// Offline tests with a mock transport. Build with the sources + src/ on the include path.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "playtestfun/playtestfun.hpp"
#include "util.hpp"

namespace fs = std::filesystem;

static int g_failed = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failed;                                                   \
        }                                                                 \
    } while (0)

struct Mock final : ptf::Transport {
    std::mutex mu;
    std::vector<ptf::HttpRequest> calls;
    int session_status = 200;
    int event_status = 200;
    std::string player_body =
        R"({"success":true,"data":{"steamId":"76561198000000000","linked":true,"accepted":true,"playtests":[{"accepted":false}]}})";
    std::string session_body =
        R"({"success":true,"data":{"accepted":true,"sessionPlaytimeSeconds":12.5,"totalPlaytimeSeconds":99.25}})";

    std::atomic<int> get_delay_ms{0};

    ptf::HttpResponse Send(const ptf::HttpRequest& r) override {
        if (r.method == "GET" && get_delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(get_delay_ms));
        std::lock_guard<std::mutex> lk(mu);
        calls.push_back(r);
        if (r.method == "GET") return {200, player_body};
        if (r.url.find("/sdk/events") != std::string::npos)
            return {event_status, event_status == 404 ? R"({"success":false,"error":{"code":"GOAL_NOT_FOUND"}})"
                                                      : R"({"success":true,"data":{"accepted":true}})"};
        return {session_status, session_body};
    }
    std::vector<ptf::HttpRequest> Events() {
        std::lock_guard<std::mutex> lk(mu);
        std::vector<ptf::HttpRequest> out;
        for (auto& c : calls)
            if (c.url.find("/sdk/events") != std::string::npos) out.push_back(c);
        return out;
    }
    size_t Count() {
        std::lock_guard<std::mutex> lk(mu);
        return calls.size();
    }
    ptf::HttpRequest Last() {
        std::lock_guard<std::mutex> lk(mu);
        return calls.back();
    }
};

static bool WaitFor(Mock& m, size_t n, int ms = 2000) {
    for (int i = 0; i < ms / 10; ++i) {
        if (m.Count() >= n) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static bool WaitEvents(Mock& m, size_t n, int ms = 3000) {
    for (int i = 0; i < ms / 10; ++i) {
        if (m.Events().size() >= n) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static bool Has(const std::string& body, const std::string& part) {
    return body.find(part) != std::string::npos;
}

static ptf::Config BaseConfig(std::shared_ptr<Mock> mock) {
    ptf::Config c;
    c.game_key = "phub_gk_test";
    c.transport = mock;
    c.open_url_on_shutdown = false;
    c.log = [](ptf::LogLevel, const std::string&) {};
    return c;
}

static void TestUtil() {
    using namespace ptf::detail;
    CHECK(IsSteamId64("76561198000000000"));
    CHECK(!IsSteamId64("7656119800000000"));
    CHECK(!IsSteamId64("7656119800000000x"));
    CHECK(FormatSeconds(61.5) == "61.500");
    CHECK(FormatSeconds(-3) == "0.000");
    CHECK(FormatSeconds(1e9) == "86400.000");

    std::string q;
    AppendQuoted(q, "a\"b\\c\n\x01");
    CHECK(q == "\"a\\\"b\\\\c\\n\\u0001\"");

    const std::string j = R"({"success":false,"error":{"code":"RATE_LIMITED","details":{"retryAfterSeconds":7}},"x":[1,{"code":"no"}]})";
    std::string_view raw;
    std::string code;
    double d = 0;
    CHECK(JsonFind(j, {"error", "code"}, raw) && JsonString(raw, code) && code == "RATE_LIMITED");
    CHECK(JsonFind(j, {"error", "details", "retryAfterSeconds"}, raw) && JsonNumber(raw, d) && d == 7);
    CHECK(!JsonFind(j, {"data", "x"}, raw));
    CHECK(JsonNumber("1834.75", d) && d > 1834.74 && d < 1834.76);
    CHECK(JsonNumber("-2.5e2", d) && d == -250);

    const std::string u = NewUuid();
    CHECK(u.size() == 36 && u[14] == '4');
    CHECK(IsoTime(std::chrono::system_clock::time_point{}) == "1970-01-01T00:00:00.000Z");
}

static void TestHeartbeatAndShutdown() {
    auto mock = std::make_shared<Mock>();
    ptf::Client client(BaseConfig(mock));
    CHECK(!client.SetSteamId(std::string("123")));
    CHECK(client.SetSteamId(76561198000000000ull));
    CHECK(WaitFor(*mock, 2));  // player check + first beat

    const auto calls = mock->calls;
    CHECK(calls[0].method == "GET" && calls[0].url.find("/sdk/game/players/76561198000000000") != std::string::npos);
    CHECK(calls[1].method == "POST" && calls[1].url == "https://api.playtest.fun/v1/sdk/sessions");
    CHECK(calls[1].authorization == "Bearer phub_gk_test");
    CHECK(calls[1].body.find("\"steam_id\":\"76561198000000000\"") != std::string::npos);
    CHECK(calls[1].body.find("\"ended_at\"") == std::string::npos);

    const ptf::Stats s = client.GetStats();
    CHECK(s.server_total_seconds == 99.25 && s.server_session_seconds == 12.5);
    CHECK(s.player_linked == 1 && s.player_accepted == 1 && s.last_http_status == 200);

    CHECK(client.SetSessionData(R"({"level":3})"));
    CHECK(!client.SetSessionData("[1,2]"));
    client.Shutdown();
    const auto last = mock->Last();
    CHECK(last.body.find("\"ended_at\"") != std::string::npos);
    CHECK(last.body.find("\"data\":{\"level\":3}") != std::string::npos);
    CHECK(last.body.find(s.session_id) != std::string::npos);
}

static void TestFlushAndPause() {
    auto mock = std::make_shared<Mock>();
    ptf::Client client(BaseConfig(mock));
    client.SetSteamId(76561198000000000ull);
    CHECK(WaitFor(*mock, 2));
    client.SetPaused(true);
    const double a = client.GetStats().local_session_seconds;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(client.GetStats().local_session_seconds == a);
    client.SetPaused(false);
    client.Flush();
    CHECK(WaitFor(*mock, 3));
}

static void TestOfflinePersistence() {
    const fs::path dir = fs::temp_directory_path() / "ptf_tests";
    std::error_code ec;
    fs::remove_all(dir, ec);

    auto offline = std::make_shared<Mock>();
    offline->session_status = 0;  // network down
    {
        ptf::Config c = BaseConfig(offline);
        c.storage_dir = dir.string();
        c.shutdown_timeout_ms = 500;
        ptf::Client client(c);
        client.SetSteamId(76561198000000000ull);
        CHECK(WaitFor(*offline, 2));
        client.Shutdown();
    }
    size_t files = 0;
    for (auto& e : fs::directory_iterator(dir)) files += e.path().extension() == ".json";
    CHECK(files == 1);

    // Next launch, online: the old session goes out first, then the file is gone.
    auto online = std::make_shared<Mock>();
    {
        ptf::Config c = BaseConfig(online);
        c.storage_dir = dir.string();
        ptf::Client client(c);
        client.SetSteamId(76561198000000000ull);
        CHECK(WaitFor(*online, 3));
        client.Shutdown();
    }
    int ended = 0;  // the old session's final beat + this run's
    for (auto& r : online->calls) ended += r.body.find("\"ended_at\"") != std::string::npos;
    CHECK(ended == 2);
    files = 0;
    for (auto& e : fs::directory_iterator(dir)) files += e.path().extension() == ".json";
    CHECK(files == 0);
    fs::remove_all(dir, ec);
}

static void TestFatalKey() {
    auto mock = std::make_shared<Mock>();
    mock->session_status = 401;
    mock->session_body = "{\"success\":false}";
    mock->event_status = 401;
    ptf::Client client(BaseConfig(mock));
    client.Goal("g1");
    client.Goal("g2");
    client.SetSteamId(76561198000000000ull);
    CHECK(WaitFor(*mock, 2));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(mock->Events().empty());                  // disabled before the drain
    CHECK(client.GetStats().pending_events == 2);   // kept for a later run, not dropped
    const size_t n = mock->Count();
    client.Shutdown();
    CHECK(mock->Count() == n);  // disabled: no final send
}

static void TestBadKeyPrefix() {
    auto mock = std::make_shared<Mock>();
    ptf::Config c = BaseConfig(mock);
    c.game_key = "phub_sk_secret";
    ptf::Client client(c);
    client.SetSteamId(76561198000000000ull);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(mock->Count() == 0);
}

static void TestCApi() {
    ptf_config c;
    ptf_config_init(&c);
    CHECK(c.struct_size == sizeof(ptf_config) && c.open_url_on_shutdown == 1);
    c.game_key = "phub_gk_test";
    c.open_url_on_shutdown = 0;
    c.http = [](void*, const char* method, const char*, const char*, const char*, char* out, size_t cap, int) -> int {
        std::snprintf(out, cap, "%s", std::string(method) == "GET"
                                          ? R"({"data":{"linked":false,"accepted":false}})"
                                          : R"({"data":{"sessionPlaytimeSeconds":1,"totalPlaytimeSeconds":2}})");
        return 200;
    };
    CHECK(ptf_init(&c) == 1);
    CHECK(ptf_set_steam_id_str("76561198000000000") == 1);
    ptf_stats s{};
    for (int i = 0; i < 200 && s.server_total_seconds != 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ptf_get_stats(&s);
    }
    CHECK(s.server_total_seconds == 2);
    CHECK(std::string(s.session_id).size() == 36);
    CHECK(ptf_goal_value("collect_points", 5) == 1);
    CHECK(ptf_event("GAME_STARTED", "OK", R"({"mode":"story"})") == 1);
    CHECK(ptf_event("GAME_STARTED", "OK", "not json") == 0);
    ptf_shutdown();
    CHECK(ptf_get_stats(&s) == 0);
    CHECK(ptf_open_url("file:///etc/passwd") == 0);
}

static void TestJsonAndSessionId() {
    ptf::JsonObject o;
    o.Set("s", "a\"b").Set("i", 3).Set("u", 7u).Set("b", true).Set("d", 1.5).Set("f", 0.25f).SetNull("n");
    o.Set("nested", ptf::JsonObject().Set("x", -2)).SetRaw("arr", "[1,2]");
    CHECK(o.str() == R"({"s":"a\"b","i":3,"u":7,"b":true,"d":1.5,"f":0.25,"n":null,"nested":{"x":-2},"arr":[1,2]})");
    CHECK(ptf::JsonObject().str() == "{}");

    ptf::JsonObject raw;
    CHECK(ptf::JsonObject::FromRaw(R"( {"a":1} )", raw) && raw.str() == R"({"a":1})");
    CHECK(ptf::JsonObject::FromRaw("{ }", raw) && raw.empty());
    CHECK(!ptf::JsonObject::FromRaw("[1]", raw));

    CHECK(ptf::detail::FormatNumber(0.1) == "0.1");
    CHECK(ptf::detail::FormatNumber(std::numeric_limits<double>::infinity()) == "null");
    CHECK(ptf::detail::TruncateUtf8("a\xC3\xBC", 2) == "a");  // never splits a 2-byte char

    using namespace std::chrono;
    const auto t = system_clock::time_point(seconds(1792061234));  // 2026-10-15T10:47:14Z
    const std::string id = ptf::detail::MakeSessionId("0.8.4a beta/1", t);
    CHECK(id.rfind("0.8.4abeta1-20261015T104714Z-", 0) == 0 && id.size() == 29 + 12);
    CHECK(ptf::detail::MakeSessionId("", t).size() == 36);
    CHECK(ptf::detail::MakeSessionId("0.8", t) != ptf::detail::MakeSessionId("0.8", t));
}

static void TestEvents() {
    auto mock = std::make_shared<Mock>();
    ptf::Config c = BaseConfig(mock);
    c.build_version = "0.8.4a";
    c.playtest_id = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
    ptf::Client client(c);

    // Queued until the SteamID is known; goal spam collapses to the highest value.
    for (int i = 0; i <= 120; ++i) CHECK(client.Goal("collect_points", i));
    CHECK(client.Goal("finish_tutorial"));
    CHECK(client.Goal("finish_tutorial"));
    CHECK(client.TestResult(12, true, "Finished the tutorial"));
    CHECK(client.Crash("NullRef@Render", "NullReferenceException", "at Render()",
                       ptf::JsonObject().Set("gpu", "RTX 3060")));
    CHECK(client.Crash("NullRef@Render"));  // same fingerprint still queued
    CHECK(!client.Event("NOT_A_TYPE", "X", {}));
    CHECK(client.GetStats().pending_events == 4);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(mock->Events().empty());

    client.SetSteamId(76561198000000000ull);
    CHECK(WaitEvents(*mock, 4));
    const auto ev = mock->Events();
    CHECK(ev.size() == 4);
    if (ev.size() < 4) return;
    CHECK(Has(ev[0].body, R"("event_type":"GOAL_PROGRESS","status":"PROGRESS")") && Has(ev[0].body, R"("value":120)"));
    CHECK(Has(ev[0].body, R"("steam_id":"76561198000000000")"));
    CHECK(Has(ev[0].body, R"("playtest_id":"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")"));
    CHECK(Has(ev[0].body, R"("event_id":")"));
    CHECK(Has(ev[1].body, R"("status":"DONE")") && Has(ev[1].body, R"({"goal":"finish_tutorial"})"));
    CHECK(Has(ev[2].body, R"({"test_id":12,"result":true,"message":"Finished the tutorial"})"));
    CHECK(Has(ev[3].body, R"("fingerprint":"NullRef@Render")") && Has(ev[3].body, R"("buildVersion":"0.8.4a")"));
    CHECK(Has(ev[3].body, R"("platform":")") && Has(ev[3].body, R"("hardwareSnapshot":{"gpu":"RTX 3060"})"));
    CHECK(client.GetStats().session_id.rfind("0.8.4a-", 0) == 0);

    client.Shutdown();
    CHECK(mock->Events().size() == 4);
}

static void TestEventRejectedIsDropped() {
    auto mock = std::make_shared<Mock>();
    mock->event_status = 404;  // GOAL_NOT_FOUND: retrying won't help
    ptf::Client client(BaseConfig(mock));
    client.SetSteamId(76561198000000000ull);
    client.Goal("old_goal_id");
    CHECK(WaitEvents(*mock, 1));
    for (int i = 0; i < 100 && client.GetStats().pending_events; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(client.GetStats().pending_events == 0);
}

static size_t GoalEvents(Mock& m) {
    size_t n = 0;
    for (auto& e : m.Events())
        if (Has(e.body, "GOAL_PROGRESS")) ++n;
    return n;
}

static void TestGoalsOfAPlayerNotAcceptedAreDropped() {
    auto mock = std::make_shared<Mock>();
    mock->player_body = R"({"success":true,"data":{"linked":true,"accepted":false}})";
    mock->session_status = 403;
    mock->session_body = R"({"success":false,"error":{"code":"PLAYER_NOT_IN_PLAYTEST"}})";
    ptf::Client client(BaseConfig(mock));

    CHECK(client.Goal("finish_tutorial"));  // status not known yet: queued
    CHECK(client.TestResult(3, true));
    client.SetSteamId(76561198000000000ull);
    CHECK(WaitEvents(*mock, 1));  // the test result; the goal is dropped before it is sent
    for (int i = 0; i < 100 && client.GetStats().pending_events; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    const ptf::Stats s = client.GetStats();
    CHECK(s.player_linked == 1 && s.player_accepted == 0);
    CHECK(s.pending_events == 0);
    CHECK(GoalEvents(*mock) == 0);
    CHECK(mock->calls[0].method == "GET");  // checked before any event went out

    CHECK(!client.Goal("finish_tutorial"));
    CHECK(!client.Goal("collect_points", 5));
    CHECK(client.TestResult(4, false));  // other events still go out
    CHECK(WaitEvents(*mock, 2));
    CHECK(GoalEvents(*mock) == 0);

    // Another player: not known yet, so goals queue again (the slow check keeps it unknown here).
    mock->get_delay_ms = 500;
    CHECK(client.SetSteamId(76561198000000001ull));
    CHECK(client.Goal("finish_tutorial"));
    CHECK(client.GetStats().player_accepted == -1);
    client.Shutdown();
}

static void TestEventQueueSurvivesRestart() {
    const fs::path dir = fs::temp_directory_path() / "ptf_tests_events";
    std::error_code ec;
    fs::remove_all(dir, ec);

    auto offline = std::make_shared<Mock>();
    offline->event_status = 503;
    offline->session_status = 0;
    {
        ptf::Config c = BaseConfig(offline);
        c.storage_dir = dir.string();
        c.shutdown_timeout_ms = 300;
        ptf::Client client(c);
        client.Crash("Boom@Main", "boom");  // on disk before returning
        CHECK(fs::exists(dir / "playtestfun_events.jsonl"));
        client.SetSteamId(76561198000000000ull);
        client.TestResult(7, false);
        CHECK(WaitEvents(*offline, 1));
        client.Shutdown();
    }

    auto online = std::make_shared<Mock>();
    {
        ptf::Config c = BaseConfig(online);
        c.storage_dir = dir.string();
        ptf::Client client(c);
        client.SetSteamId(76561198000000000ull);
        CHECK(WaitEvents(*online, 2));
        for (int i = 0; i < 100 && client.GetStats().pending_events; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        client.Shutdown();
    }
    const auto ev = online->Events();
    CHECK(ev.size() == 2);
    if (ev.size() < 2) return;
    CHECK(Has(ev[0].body, "Boom@Main") && Has(ev[1].body, R"("test_id":7)"));
    // Same event_id as the failed attempt, so the server dedupes it.
    const std::string first = offline->Events()[0].body;
    const auto at = first.find(R"("event_id":")");
    CHECK(at != std::string::npos && Has(ev[0].body, first.substr(at, 50)));
    CHECK(!fs::exists(dir / "playtestfun_events.jsonl"));
    fs::remove_all(dir, ec);
}

// A crash storm with distinct fingerprints: capped, and the capped calls never rewrite the file.
static void TestQueueLimits() {
    const fs::path dir = fs::temp_directory_path() / "ptf_tests_limits";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto mock = std::make_shared<Mock>();
    ptf::Config c = BaseConfig(mock);
    c.storage_dir = dir.string();
    c.shutdown_timeout_ms = 100;
    const fs::path file = dir / "playtestfun_events.jsonl";
    {
        ptf::Client client(c);  // no SteamID: everything stays queued
        const std::string stack(15000, 's');
        for (int i = 0; i < 20; ++i) CHECK(client.Crash("Index " + std::to_string(i) + "@Update", "m", stack));
        const auto written = fs::last_write_time(file, ec);
        const auto size = fs::file_size(file, ec);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        for (int i = 20; i < 200; ++i) CHECK(!client.Crash("Index " + std::to_string(i) + "@Update", "m", stack));
        CHECK(client.Crash("Index 3@Update"));  // already queued: still true
        CHECK(fs::last_write_time(file, ec) == written && fs::file_size(file, ec) == size);
        CHECK(client.GetStats().pending_events == 20);

        // Bulky events: the file stays under 1 MB, oldest non-crash dropped first, crashes kept.
        for (int i = 0; i < 1500; ++i)
            client.Event("GAME_STARTED", "DONE", ptf::JsonObject().Set("n", i).Set("pad", std::string(1000, 'p')));
        const auto st = client.GetStats();
        CHECK(st.pending_events > 20 && st.pending_events < 1000);
        client.Shutdown();
        CHECK(fs::file_size(file, ec) <= 1024 * 1024 + 64 * 1024);
    }
    std::string text;
    {
        FILE* f = std::fopen(file.string().c_str(), "rb");
        CHECK(f != nullptr);
        if (f) {
            char buf[65536];
            for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
            std::fclose(f);
        }
    }
    size_t crashes = 0;
    for (size_t at = 0; (at = text.find("\"CRASH\"", at)) != std::string::npos; ++at) ++crashes;
    CHECK(crashes == 20);
    CHECK(Has(text, R"("n":1499)") && !Has(text, R"("n":0,)"));
    fs::remove_all(dir, ec);
}

int main() {
    TestUtil();
    TestQueueLimits();
    TestJsonAndSessionId();
    TestEvents();
    TestEventRejectedIsDropped();
    TestGoalsOfAPlayerNotAcceptedAreDropped();
    TestEventQueueSurvivesRestart();
    TestHeartbeatAndShutdown();
    TestFlushAndPause();
    TestOfflinePersistence();
    TestFatalKey();
    TestBadKeyPrefix();
    TestCApi();
    std::printf(g_failed ? "%d check(s) failed\n" : "all tests passed\n", g_failed);
    return g_failed ? 1 : 0;
}
