// Internal client state shared by client.cpp, session.cpp and events.cpp.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <future>
#include <mutex>
#include <string>
#include <thread>

#include "playtestfun/playtestfun.hpp"

namespace ptf {

namespace detail {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr int kRequestTimeoutMs = 10000;

enum class Outcome { Ok, Retry, Rejected, Fatal };

struct QueuedEvent {
    std::string event_id;
    std::string type;
    std::string status;
    std::string timestamp;
    std::string steam_id;  // empty = stamped at send time
    std::string data;      // JSON object
    // Coalescing keys.
    std::string goal;       // GOAL_PROGRESS goal id
    double goal_value = 0;
    bool has_goal_value = false;
    std::string fingerprint;  // CRASH
};

}  // namespace detail

struct Client::State {
    Config cfg;
    std::shared_ptr<Transport> transport;
    std::string auth;
    std::string session_id;
    std::string started_at;
    detail::fs::path storage;  // empty = no persistence

    mutable std::mutex mu;
    std::condition_variable cv;
    // Guarded by mu.
    std::string steam_id;
    std::string session_data;
    bool paused = false;
    bool stopping = false;
    bool wake = false;        // heartbeat now
    bool event_kick = false;  // events queued
    bool disabled = false;
    bool player_checked = false;
    detail::Clock::time_point active_since = detail::Clock::now();
    double banked = 0;
    Stats stats;
    std::deque<detail::QueuedEvent> events;
    std::string in_flight;  // event_id being sent; never coalesced into
    bool events_dirty = false;

    std::mutex io_mu;  // serializes writes of the event file

    // Worker thread; owned by Client, detached only on shutdown timeout.
    std::thread thread;
    std::future<void> done;
    std::atomic<bool> muted{false};  // host callbacks may be gone after a detach

    // Worker only.
    int failures = 0;
    int event_failures = 0;
    double last_sent = -1;
    bool pending_on_disk = false;
    bool scan_old = true;

    double ElapsedLocked() const {
        return banked +
               (paused ? 0.0 : std::chrono::duration<double>(detail::Clock::now() - active_since).count());
    }

    void Log(LogLevel level, const std::string& msg) const {
        if (muted.load(std::memory_order_relaxed)) return;
        if (cfg.log) cfg.log(level, msg);
        else if (level >= LogLevel::Warn) std::fprintf(stderr, "[playtestfun] %s\n", msg.c_str());
    }
};

namespace detail {

using State = Client::State;

// io (client.cpp)
fs::path Utf8Path(const std::string& s);
bool WriteFileAtomic(const fs::path& path, const std::string& data);
bool ReadFile(const fs::path& path, std::string& out);

// http (client.cpp)
HttpResponse Request(State& s, const char* method, const std::string& path, std::string body, int timeout_ms);
std::string ErrorCode(const std::string& body);
int RetryAfterSeconds(const std::string& body);  // 0 if absent
Outcome Classify(int status, const std::string& code);
void RecordStatus(State& s, int status, const std::string& code);

// sessions (session.cpp)
std::string BuildSessionBodyLocked(const State& s, bool ended);
void CheckPlayer(State& s, const std::string& steam_id);
Outcome SendSession(State& s, const std::string& body, int timeout_ms, int& retry_after);
void PersistPending(State& s, const std::string& body);
void ClearPending(State& s);
void SendOldSessions(State& s);

// events (events.cpp)
bool EnqueueEventLocked(State& s, QueuedEvent ev);
void LoadEvents(State& s);
void PersistEvents(State& s);
// A goal of the current player while the server says that player is not accepted: never counted.
bool GoalNotCountedLocked(const State& s, const QueuedEvent& ev, const std::string& steam_id);
// Sends queued events in order until empty, a failure, max_count or the deadline.
// Returns when to try again, or zero when nothing is waiting to be sent.
Clock::duration DrainEvents(State& s, Clock::time_point deadline, int max_count);

}  // namespace detail

}  // namespace ptf
