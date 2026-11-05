// POST /sdk/events: ordered queue, coalescing, disk persistence, retries.
#include <algorithm>
#include <sstream>
#include <unordered_set>

#include "state.hpp"
#include "util.hpp"

namespace ptf::detail {

namespace {

constexpr size_t kMaxQueued = 1000;
constexpr const char* kEventsFile = "playtestfun_events.jsonl";
constexpr std::chrono::seconds kMaxEventBackoff{60};

// One JSON line: the API body without playtest_id (added at send time).
std::string Serialize(const QueuedEvent& e, const std::string& steam_id, const std::string& playtest_id) {
    std::string b;
    b.reserve(160 + e.data.size());
    b += "{\"event_type\":";
    AppendQuoted(b, e.type);
    b += ",\"status\":";
    AppendQuoted(b, e.status);
    b += ",\"timestamp\":";
    AppendQuoted(b, e.timestamp);
    b += ",\"event_id\":";
    AppendQuoted(b, e.event_id);
    if (!steam_id.empty()) {
        b += ",\"steam_id\":";
        AppendQuoted(b, steam_id);
    }
    if (!playtest_id.empty()) {
        b += ",\"playtest_id\":";
        AppendQuoted(b, playtest_id);
    }
    b += ",\"data\":";
    b += e.data.empty() ? "{}" : e.data;
    b += '}';
    return b;
}

bool Field(std::string_view json, std::string_view key, std::string& out) {
    std::string_view raw;
    return JsonFind(json, {key}, raw) && JsonString(raw, out);
}

bool Parse(std::string_view line, QueuedEvent& e) {
    if (!Field(line, "event_id", e.event_id) || !Field(line, "event_type", e.type) ||
        !Field(line, "status", e.status) || !Field(line, "timestamp", e.timestamp))
        return false;
    Field(line, "steam_id", e.steam_id);
    std::string_view raw;
    if (!JsonFind(line, {"data"}, raw) || raw.empty() || raw.front() != '{') return false;
    e.data = std::string(raw);
    if (e.type == "GOAL_PROGRESS") {
        Field(e.data, "goal", e.goal);
        e.has_goal_value = JsonFind(e.data, {"value"}, raw) && JsonNumber(raw, e.goal_value);
    } else if (e.type == "CRASH") {
        Field(e.data, "fingerprint", e.fingerprint);
    }
    return true;
}

fs::path EventsPath(const State& s) {
    return s.storage / kEventsFile;
}

}  // namespace

bool EnqueueEventLocked(State& s, QueuedEvent ev) {
    for (auto& q : s.events) {
        if (q.event_id == s.in_flight || q.type != ev.type) continue;
        // Goal spam (every frame) collapses into one pending event with the best value.
        if (!ev.goal.empty() && q.goal == ev.goal && q.has_goal_value == ev.has_goal_value) {
            if (ev.has_goal_value && ev.goal_value > q.goal_value) {
                q.goal_value = ev.goal_value;
                q.data = ev.data;
                q.timestamp = ev.timestamp;
                s.events_dirty = true;
            }
            return true;
        }
        if (!ev.fingerprint.empty() && q.fingerprint == ev.fingerprint) return true;
    }
    if (s.events.size() >= kMaxQueued) {
        // Drop the oldest that isn't on the wire.
        const size_t i = !s.events.empty() && s.events.front().event_id == s.in_flight ? 1 : 0;
        if (i < s.events.size()) s.events.erase(s.events.begin() + static_cast<std::ptrdiff_t>(i));
    }
    s.events.push_back(std::move(ev));
    s.events_dirty = true;
    s.event_kick = true;
    return true;
}

void LoadEvents(State& s) {
    if (s.storage.empty()) return;
    std::string text;
    {
        std::lock_guard<std::mutex> io(s.io_mu);
        if (!ReadFile(EventsPath(s), text)) return;
    }

    std::lock_guard<std::mutex> lk(s.mu);
    std::unordered_set<std::string> seen;
    for (const auto& q : s.events) seen.insert(q.event_id);  // queued this run already

    std::deque<QueuedEvent> loaded;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        QueuedEvent e;
        if (Parse(line, e) && seen.insert(e.event_id).second) loaded.push_back(std::move(e));
    }
    if (loaded.empty()) return;

    // Older events go first.
    for (auto it = loaded.rbegin(); it != loaded.rend(); ++it) s.events.push_front(std::move(*it));
    while (s.events.size() > kMaxQueued) s.events.pop_front();
    s.event_kick = true;
    s.Log(LogLevel::Debug, "loaded " + std::to_string(loaded.size()) + " queued event(s) from disk");
}

void PersistEvents(State& s) {
    std::lock_guard<std::mutex> io(s.io_mu);  // snapshot under io_mu: the last writer has the latest state
    std::string text;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        if (!s.events_dirty) return;
        s.events_dirty = false;
        if (s.storage.empty()) return;
        for (const auto& e : s.events) {
            text += Serialize(e, e.steam_id, {});
            text += '\n';
        }
    }
    std::error_code ec;
    if (text.empty()) fs::remove(EventsPath(s), ec);
    else if (!WriteFileAtomic(EventsPath(s), text)) s.Log(LogLevel::Warn, "could not write the event queue");
}

bool GoalNotCountedLocked(const State& s, const QueuedEvent& ev, const std::string& steam_id) {
    // Only for the current player, whose status the last check or heartbeat reported.
    return ev.type == "GOAL_PROGRESS" && s.stats.player_accepted == 0 && !steam_id.empty() && steam_id == s.steam_id;
}

Clock::duration DrainEvents(State& s, Clock::time_point deadline, int max_count) {
    for (int sent = 0; sent < max_count; ++sent) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left < 200) return std::chrono::seconds(1);

        QueuedEvent ev;
        std::string steam_id;
        bool dropped = false;
        {
            std::lock_guard<std::mutex> lk(s.mu);
            if (s.events.empty() || s.disabled) return Clock::duration::zero();
            ev = s.events.front();
            steam_id = ev.steam_id.empty() ? s.steam_id : ev.steam_id;
            if (steam_id.empty()) return Clock::duration::zero();  // waits for SetSteamId
            dropped = GoalNotCountedLocked(s, ev, steam_id);
            if (dropped) {
                s.events.pop_front();  // queued before the player check came back
                s.events_dirty = true;
            } else {
                s.in_flight = ev.event_id;
            }
        }
        if (dropped) {
            s.Log(LogLevel::Debug, "goal dropped: the player is not accepted into this game's playtest");
            continue;
        }

        const int timeout = static_cast<int>(std::min<long long>(kRequestTimeoutMs, left));
        HttpResponse r = Request(s, "POST", "/sdk/events", Serialize(ev, steam_id, s.cfg.playtest_id), timeout);
        const std::string code = ErrorCode(r.body);
        const Outcome out = Classify(r.status, code);

        {
            std::lock_guard<std::mutex> lk(s.mu);
            s.in_flight.clear();
            // Fatal (bad/revoked key, suspended studio) keeps the event on disk for a later build/run.
            const bool done_with = out == Outcome::Ok || out == Outcome::Rejected;
            if (done_with && !s.events.empty() && s.events.front().event_id == ev.event_id) {
                s.events.pop_front();
                s.events_dirty = true;
            }
            if (out == Outcome::Fatal) s.disabled = true;
        }

        if (out == Outcome::Fatal) {
            s.Log(LogLevel::Error, "game key rejected; reporting is off for this run");
            return Clock::duration::zero();
        }
        if (out == Outcome::Retry) {
            ++s.event_failures;
            auto d = std::min<std::chrono::seconds>(std::chrono::seconds(1LL << std::min(s.event_failures - 1, 6)),
                                                    kMaxEventBackoff);
            if (r.status == 429) d = std::max<std::chrono::seconds>(d, std::chrono::seconds(RetryAfterSeconds(r.body)));
            return d;
        }
        s.event_failures = 0;
        if (out == Outcome::Rejected)  // won't change on retry: dropped
            s.Log(LogLevel::Warn, ev.type + " rejected: HTTP " + std::to_string(r.status) +
                                      (code.empty() ? "" : " " + code));
    }
    return std::chrono::seconds(1);  // more may be waiting; spread bursts out
}

}  // namespace ptf::detail
