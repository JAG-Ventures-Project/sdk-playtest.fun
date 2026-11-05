// Client API, the worker loop and shared io/http helpers.
#include <algorithm>
#include <fstream>

#include "state.hpp"
#include "util.hpp"

namespace ptf {

namespace detail {

namespace {

constexpr size_t kMaxSessionData = 16 * 1024;
constexpr size_t kMaxEventData = 32 * 1024;
constexpr int kEventsPerPass = 20;  // then yield 1 s: the rate limit is shared by all players
constexpr std::chrono::seconds kSlowRetry{300};  // player not linked/accepted yet
constexpr std::chrono::seconds kMaxBackoff{600};

using std::chrono::milliseconds;
using std::chrono::seconds;

Clock::duration Jittered(seconds base) {
    return std::chrono::duration_cast<Clock::duration>(base * (0.9 + 0.2 * RandomUnit()));
}

Clock::time_point Far() {
    return Clock::now() + std::chrono::hours(1);
}

// Spreads load across many players sharing one game key.
Clock::duration NextBeat(State& s, Outcome out, int retry_after) {
    const seconds beat{std::max<uint32_t>(s.cfg.heartbeat_seconds, 10)};
    switch (out) {
        case Outcome::Ok:
            s.failures = 0;
            return Jittered(beat);
        case Outcome::Retry: {
            ++s.failures;
            seconds d = std::min<seconds>(beat * (1 << std::min(s.failures - 1, 4)), kMaxBackoff);
            if (retry_after > 0) d = std::max<seconds>(d, seconds(retry_after));
            return Jittered(d);
        }
        default:
            return Jittered(std::max(beat, kSlowRetry));
    }
}

Outcome Heartbeat(State& s, const std::string& steam_id, const std::string& body, double elapsed, bool check,
                  int& retry_after) {
    if (check) CheckPlayer(s, steam_id);
    if (s.scan_old) SendOldSessions(s);
    if (elapsed == s.last_sent) return Outcome::Ok;  // paused and unchanged

    const Outcome out = SendSession(s, body, kRequestTimeoutMs, retry_after);
    if (out == Outcome::Ok) s.last_sent = elapsed;
    if (out == Outcome::Retry) PersistPending(s, body);  // survives a crash while offline
    else ClearPending(s);
    return out;
}

void Worker(std::shared_ptr<State> sp, std::promise<void> done) {
    State& s = *sp;
    LoadEvents(s);

    std::unique_lock<std::mutex> lk(s.mu);
    Clock::time_point next_beat = Clock::now() + Jittered(seconds(s.cfg.heartbeat_seconds));
    Clock::time_point next_events = Far();
    Clock::time_point last_drain{};

    while (!s.stopping) {
        s.cv.wait_until(lk, std::min(next_beat, next_events), [&] { return s.stopping || s.wake || s.event_kick; });
        if (s.stopping) break;
        if (s.event_kick) {
            s.event_kick = false;
            next_events = std::min(next_events, last_drain + seconds(1));  // batch bursts
        }
        const Clock::time_point now = Clock::now();
        const bool beat = s.wake || now >= next_beat;
        const bool drain = now >= next_events;
        s.wake = false;
        if (!beat && !drain && !s.events_dirty) continue;

        const bool can_send = !s.disabled && !s.steam_id.empty();
        const double elapsed = s.ElapsedLocked();
        const bool check = !s.player_checked;
        const std::string steam_id = s.steam_id;
        const std::string body = beat && can_send ? BuildSessionBodyLocked(s, false) : std::string();
        lk.unlock();

        PersistEvents(s);

        Outcome out = Outcome::Ok;
        int retry_after = 0;
        if (beat) {
            if (can_send) out = Heartbeat(s, steam_id, body, elapsed, check, retry_after);
            next_beat = Clock::now() + (can_send ? NextBeat(s, out, retry_after) : Jittered(seconds(s.cfg.heartbeat_seconds)));
            if (out == Outcome::Fatal) {
                std::lock_guard<std::mutex> g(s.mu);
                s.disabled = true;  // before the drain below
                s.Log(LogLevel::Error, "game key rejected; reporting is off for this run");
            }
        }
        if (drain) {
            // Know whether the player counts before the first goal goes out (goals of a player
            // who is not accepted are dropped locally, see DrainEvents).
            if (can_send && check && !beat) CheckPlayer(s, steam_id);
            last_drain = Clock::now();
            const Clock::duration again = can_send ? DrainEvents(s, Far(), kEventsPerPass) : Clock::duration::zero();
            next_events = again == Clock::duration::zero() ? Far() : Clock::now() + again;
            PersistEvents(s);
        }

        lk.lock();
    }

    // Final beat with ended_at, then queued events, all within shutdown_timeout_ms.
    // Everything is on disk first so a hard exit still keeps it.
    const Clock::time_point deadline = Clock::now() + milliseconds(s.cfg.shutdown_timeout_ms);
    const bool can_send = !s.disabled && !s.steam_id.empty();
    const std::string body = can_send ? BuildSessionBodyLocked(s, true) : std::string();
    lk.unlock();

    PersistEvents(s);
    if (can_send) {
        PersistPending(s, body);
        int retry_after = 0;
        const int timeout = static_cast<int>(std::max<uint32_t>(s.cfg.shutdown_timeout_ms, 500));
        if (SendSession(s, body, timeout, retry_after) != Outcome::Retry) ClearPending(s);
        DrainEvents(s, deadline, 1 << 30);
        PersistEvents(s);
    }
    done.set_value();
}

QueuedEvent NewEvent(std::string type, std::string status, std::string data) {
    QueuedEvent e;
    e.event_id = NewUuid();
    e.type = std::move(type);
    e.status = std::move(status);
    e.timestamp = IsoTime(std::chrono::system_clock::now());
    e.data = std::move(data);
    return e;
}

bool IsEventType(const std::string& t) {
    return t == "TEST_RESULT" || t == "CRASH" || t == "GOAL_PROGRESS" || t == "GAME_STARTED" || t == "GAME_FINISHED";
}

}  // namespace

// io

fs::path Utf8Path(const std::string& s) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));  // u8path is deprecated in C++20
#else
    return fs::u8path(s);
#endif
}

bool WriteFileAtomic(const fs::path& path, const std::string& data) {
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) fs::remove(tmp, ec);
    return !ec;
}

bool ReadFile(const fs::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// http

HttpResponse Request(State& s, const char* method, const std::string& path, std::string body, int timeout_ms) {
    HttpRequest req;
    req.method = method;
    req.url = s.cfg.base_url + path;
    req.authorization = s.auth;
    req.body = std::move(body);
    req.timeout_ms = timeout_ms;
    return s.transport->Send(req);
}

std::string ErrorCode(const std::string& body) {
    std::string_view raw;
    std::string code;
    if (JsonFind(body, {"error", "code"}, raw)) JsonString(raw, code);
    return code;
}

int RetryAfterSeconds(const std::string& body) {
    std::string_view raw;
    double wait = 0;
    if (JsonFind(body, {"error", "details", "retryAfterSeconds"}, raw) && JsonNumber(raw, wait) && wait > 0)
        return static_cast<int>(std::min(wait, 3600.0)) + 1;
    return 0;
}

Outcome Classify(int status, const std::string& code) {
    if (status >= 200 && status < 300) return Outcome::Ok;
    if (status == 0 || status == 408 || status == 429 || status >= 500) return Outcome::Retry;
    if (status == 401 || (status == 403 && code.empty())) return Outcome::Fatal;  // bad or revoked key
    return Outcome::Rejected;
}

void RecordStatus(State& s, int status, const std::string& code) {
    std::lock_guard<std::mutex> lk(s.mu);
    s.stats.last_http_status = status;
    s.stats.last_error = code;
    if (code == "PLAYER_NOT_LINKED") s.stats.player_linked = s.stats.player_accepted = 0;
    else if (code == "PLAYER_NOT_IN_PLAYTEST") s.stats.player_linked = 1, s.stats.player_accepted = 0;
}

}  // namespace detail

using detail::Clock;
using detail::State;

Client::Client(Config config) : state_(std::make_shared<State>()) {
    State& s = *state_;
    s.cfg = std::move(config);
    while (!s.cfg.base_url.empty() && s.cfg.base_url.back() == '/') s.cfg.base_url.pop_back();
    if (s.cfg.heartbeat_seconds == 0) s.cfg.heartbeat_seconds = 60;
    if (s.cfg.shutdown_timeout_ms == 0) s.cfg.shutdown_timeout_ms = 2000;

    const auto now = std::chrono::system_clock::now();
    s.transport = s.cfg.transport ? s.cfg.transport : CreateDefaultTransport();
    s.auth = "Bearer " + s.cfg.game_key;
    s.session_id = detail::MakeSessionId(s.cfg.build_version, now);
    s.started_at = detail::IsoTime(now);
    s.stats.session_id = s.session_id;

    if (!s.cfg.storage_dir.empty()) {
        std::error_code ec;
        s.storage = detail::Utf8Path(s.cfg.storage_dir);
        detail::fs::create_directories(s.storage, ec);
    }

    if (!s.transport) {
        s.disabled = true;
        s.Log(LogLevel::Error, "no HTTP transport for this platform; pass Config::transport");
    } else if (s.cfg.game_key.rfind("phub_gk_", 0) != 0) {
        s.disabled = true;
        s.Log(LogLevel::Error, "game_key must start with phub_gk_ (never ship a studio key)");
    }
    if (s.disabled) return;

    std::promise<void> done;
    s.done = done.get_future();
    s.thread = std::thread(detail::Worker, state_, std::move(done));
}

Client::~Client() {
    Shutdown();
}

bool Client::SetSteamId(uint64_t steam_id64) {
    return SetSteamId(std::to_string(steam_id64));
}

bool Client::SetSteamId(const std::string& steam_id64) {
    if (!state_ || !detail::IsSteamId64(steam_id64)) return false;
    State& s = *state_;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        if (s.stopping || s.disabled) return false;
        if (s.steam_id == steam_id64) return true;
        s.steam_id = steam_id64;
        s.player_checked = false;
        s.stats.player_linked = s.stats.player_accepted = -1;  // another player: not known yet
        s.wake = true;        // first beat right away
        s.event_kick = true;  // events waiting for the id
    }
    s.cv.notify_one();
    return true;
}

void Client::SetPaused(bool paused) {
    if (!state_) return;
    State& s = *state_;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        if (s.paused == paused || s.stopping) return;
        if (paused) {
            s.banked = s.ElapsedLocked();
            s.wake = true;  // the OS may kill a backgrounded app: send now
        } else {
            s.active_since = Clock::now();
        }
        s.paused = paused;
    }
    if (paused) s.cv.notify_one();
}

bool Client::SetSessionData(const JsonObject& data) {
    return SetSessionData(data.empty() ? std::string() : data.str());
}

bool Client::SetSessionData(std::string json_object) {
    if (!state_ || json_object.size() > detail::kMaxSessionData) return false;
    const auto first = json_object.find_first_not_of(" \t\r\n");
    if (!json_object.empty() && (first == std::string::npos || json_object[first] != '{')) return false;
    std::lock_guard<std::mutex> lk(state_->mu);
    state_->session_data = std::move(json_object);
    return true;
}

bool Client::Goal(const std::string& key) {
    if (key.empty()) return false;
    auto e = detail::NewEvent("GOAL_PROGRESS", "DONE", JsonObject().Set("goal", key).str());
    e.goal = key;
    return Enqueue(std::move(e));
}

bool Client::Goal(const std::string& key, double value) {
    if (key.empty()) return false;
    auto e = detail::NewEvent("GOAL_PROGRESS", "PROGRESS", JsonObject().Set("goal", key).Set("value", value).str());
    e.goal = key;
    e.goal_value = value;
    e.has_goal_value = true;
    return Enqueue(std::move(e));
}

bool Client::TestResult(int test_id, bool passed, const std::string& message, const std::string& reward) {
    JsonObject d;
    d.Set("test_id", test_id).Set("result", passed);
    if (!message.empty()) d.Set("message", detail::TruncateUtf8(message, 500));
    if (!reward.empty()) d.Set("reward", detail::TruncateUtf8(reward, 120));
    return Enqueue(detail::NewEvent("TEST_RESULT", "DONE", d.str()));
}

bool Client::Crash(const std::string& fingerprint, const std::string& message, const std::string& stack,
                   const JsonObject& hardware) {
    if (fingerprint.empty() || !state_) return false;
    JsonObject d;
    d.Set("fingerprint", detail::TruncateUtf8(fingerprint, 256));
    if (!message.empty()) d.Set("message", detail::TruncateUtf8(message, 500));
    if (!stack.empty()) d.Set("stack", detail::TruncateUtf8(stack, 20000));
    if (!state_->cfg.build_version.empty()) d.Set("buildVersion", detail::TruncateUtf8(state_->cfg.build_version, 64));
    d.Set("platform", detail::PlatformName());
    if (!hardware.empty()) d.Set("hardwareSnapshot", hardware);

    auto e = detail::NewEvent("CRASH", "FATAL", d.str());
    e.fingerprint = detail::TruncateUtf8(fingerprint, 256);
    if (!Enqueue(std::move(e))) return false;
    detail::PersistEvents(*state_);  // the process may be about to die
    return true;
}

bool Client::Event(const std::string& event_type, const std::string& status, const JsonObject& data) {
    if (!detail::IsEventType(event_type) || status.empty() || status.size() > 64) return false;
    if (data.size_bytes() > detail::kMaxEventData) return false;
    return Enqueue(detail::NewEvent(event_type, status, data.str()));
}

bool Client::Enqueue(detail::QueuedEvent&& ev) {
    if (!state_) return false;
    State& s = *state_;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        if (s.stopping || s.disabled) return false;
        if (detail::GoalNotCountedLocked(s, ev, s.steam_id)) return false;  // would only be rejected
        ev.steam_id = s.steam_id;
        if (!detail::EnqueueEventLocked(s, std::move(ev))) return false;
    }
    s.cv.notify_one();
    return true;
}

void Client::Flush() {
    if (!state_) return;
    {
        std::lock_guard<std::mutex> lk(state_->mu);
        state_->wake = true;
        state_->event_kick = true;
    }
    state_->cv.notify_one();
}

Stats Client::GetStats() const {
    if (!state_) return {};
    std::lock_guard<std::mutex> lk(state_->mu);
    Stats out = state_->stats;
    out.local_session_seconds = state_->ElapsedLocked();
    out.pending_events = state_->events.size();
    return out;
}

void Client::Shutdown() {
    if (!state_) return;
    std::shared_ptr<State> sp = std::move(state_);
    State& s = *sp;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.banked = s.ElapsedLocked();  // freeze play time at quit
        s.paused = true;
        s.stopping = true;
    }
    s.cv.notify_all();

    if (s.thread.joinable()) {
        // Bounded wait: a hung network must never block the game from closing.
        const auto budget = std::chrono::milliseconds(s.cfg.shutdown_timeout_ms + 500);
        if (s.done.wait_for(budget) == std::future_status::ready) {
            s.thread.join();
        } else {
            s.muted = true;
            s.thread.detach();  // worker keeps State alive; everything unsent is on disk
        }
    }

    if (s.cfg.open_url_on_shutdown && !s.cfg.exit_url.empty()) OpenUrl(s.cfg.exit_url);
}

}  // namespace ptf
