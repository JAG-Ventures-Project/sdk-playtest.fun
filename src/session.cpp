// POST /sdk/sessions, player status check and unsent-session files.
#include <algorithm>
#include <vector>

#include "state.hpp"
#include "util.hpp"

namespace ptf::detail {

namespace {

constexpr size_t kMaxPendingFiles = 32;
constexpr const char* kPendingPrefix = "playtestfun_session_";

fs::path PendingPath(const State& s) {
    return s.storage / (std::string(kPendingPrefix) + s.session_id + ".json");
}

}  // namespace

std::string BuildSessionBodyLocked(const State& s, bool ended) {
    std::string b;
    b.reserve(256 + s.session_data.size());
    b += "{\"steam_id\":";
    AppendQuoted(b, s.steam_id);
    if (!s.cfg.playtest_id.empty()) {
        b += ",\"playtest_id\":";
        AppendQuoted(b, s.cfg.playtest_id);
    }
    b += ",\"session_id\":";
    AppendQuoted(b, s.session_id);
    b += ",\"playtime_seconds\":";
    b += FormatSeconds(s.ElapsedLocked());
    b += ",\"started_at\":";
    AppendQuoted(b, s.started_at);
    if (ended) {
        b += ",\"ended_at\":";
        AppendQuoted(b, IsoTime(std::chrono::system_clock::now()));
    }
    if (!s.session_data.empty()) {
        b += ",\"data\":";
        b += s.session_data;
    }
    b += '}';
    return b;
}

// GET /sdk/game/players/:steamId - tells the game early if the player can't be counted.
void CheckPlayer(State& s, const std::string& steam_id) {
    HttpResponse r = Request(s, "GET", "/sdk/game/players/" + steam_id, {}, kRequestTimeoutMs);
    const std::string code = ErrorCode(r.body);
    if (Classify(r.status, code) == Outcome::Retry) return;

    bool linked = false, accepted = false;
    std::string_view raw;
    if (r.status == 200) {
        if (JsonFind(r.body, {"data", "linked"}, raw)) JsonBool(raw, linked);
        if (JsonFind(r.body, {"data", "accepted"}, raw)) JsonBool(raw, accepted);
    }
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.player_checked = true;
        if (r.status == 200 || code == "PLAYER_NOT_LINKED") {
            s.stats.player_linked = linked ? 1 : 0;
            s.stats.player_accepted = accepted ? 1 : 0;
        }
    }
    if (r.status == 200 && !accepted)
        s.Log(LogLevel::Warn, linked ? "player is not accepted into a playtest of this game; play time won't count"
                                     : "Steam account is not linked on playtest.fun; play time won't count");
    else if (r.status == 404 && code == "PLAYER_NOT_LINKED")
        s.Log(LogLevel::Warn, "Steam account is not linked on playtest.fun; play time won't count");
}

Outcome SendSession(State& s, const std::string& body, int timeout_ms, int& retry_after) {
    HttpResponse r = Request(s, "POST", "/sdk/sessions", body, timeout_ms);
    const std::string code = ErrorCode(r.body);
    const Outcome out = Classify(r.status, code);
    RecordStatus(s, r.status, code);

    if (out == Outcome::Ok) {
        double session = 0, total = 0;
        std::string_view raw;
        if (JsonFind(r.body, {"data", "sessionPlaytimeSeconds"}, raw)) JsonNumber(raw, session);
        if (JsonFind(r.body, {"data", "totalPlaytimeSeconds"}, raw)) JsonNumber(raw, total);
        std::lock_guard<std::mutex> lk(s.mu);
        s.stats.server_session_seconds = session;
        s.stats.server_total_seconds = total;
        s.stats.player_linked = s.stats.player_accepted = 1;
    } else if (r.status == 429) {
        retry_after = RetryAfterSeconds(r.body);
    }
    if (out == Outcome::Rejected || out == Outcome::Fatal)
        s.Log(LogLevel::Warn, "session rejected: HTTP " + std::to_string(r.status) + (code.empty() ? "" : " " + code));
    else if (out == Outcome::Retry)
        s.Log(LogLevel::Debug, "session send failed: HTTP " + std::to_string(r.status));
    return out;
}

void PersistPending(State& s, const std::string& body) {
    if (s.storage.empty()) return;
    if (WriteFileAtomic(PendingPath(s), body)) s.pending_on_disk = true;
}

void ClearPending(State& s) {
    if (!s.pending_on_disk) return;
    std::error_code ec;
    fs::remove(PendingPath(s), ec);
    s.pending_on_disk = false;
}

// Resend sessions a previous run could not deliver (offline, crash, killed).
void SendOldSessions(State& s) {
    s.scan_old = false;
    if (s.storage.empty()) return;

    std::vector<fs::directory_entry> files;
    std::error_code ec;
    for (fs::directory_iterator it(s.storage, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind(kPendingPrefix, 0) == 0 && it->path().extension() == ".json" &&
            name.find(s.session_id) == std::string::npos)
            files.push_back(*it);
    }
    // Newest first; drop the overflow.
    std::sort(files.begin(), files.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        std::error_code e1, e2;
        return a.last_write_time(e1) > b.last_write_time(e2);
    });
    for (size_t i = kMaxPendingFiles; i < files.size(); ++i) fs::remove(files[i].path(), ec);
    if (files.size() > kMaxPendingFiles) files.resize(kMaxPendingFiles);

    for (const auto& f : files) {
        std::string body;
        if (!ReadFile(f.path(), body)) continue;
        int retry_after = 0;
        if (SendSession(s, body, kRequestTimeoutMs, retry_after) == Outcome::Retry) {
            s.scan_old = true;  // still offline, try again later
            return;
        }
        fs::remove(f.path(), ec);
    }
}

}  // namespace ptf::detail
