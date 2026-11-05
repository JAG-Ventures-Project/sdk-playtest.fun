// PLAYTEST.FUN SDK - C++ API. Link statically or compile the sources into your engine.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "json.hpp"
#include "playtestfun.h"
#include "transport.hpp"

namespace ptf {

namespace detail { struct QueuedEvent; }

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };
using LogFn = std::function<void(LogLevel, const std::string&)>;

struct Config {
    std::string game_key;                              // phub_gk_... (write-only, safe to ship)
    std::string playtest_id;                           // optional; server picks one when empty
    std::string build_version;                         // e.g. "0.8.4a": session id prefix + crash buildVersion
    std::string base_url = "https://api.playtest.fun/v1";
    std::string storage_dir;                           // unsent sessions/events are kept here; empty = memory only
    std::string exit_url = "https://playtest.fun";
    bool open_url_on_shutdown = true;
    uint32_t heartbeat_seconds = 60;
    uint32_t shutdown_timeout_ms = 2000;
    std::shared_ptr<Transport> transport;              // nullptr = CreateDefaultTransport()
    LogFn log;                                         // nullptr = warnings to stderr
};

struct Stats {
    double local_session_seconds = 0;
    double server_session_seconds = 0;
    double server_total_seconds = 0;
    int last_http_status = 0;
    int player_linked = -1;    // -1 unknown, 0 no, 1 yes
    int player_accepted = -1;  // -1 unknown, 0 no, 1 yes
    std::string last_error;
    std::string session_id;
    size_t pending_events = 0;
};

// One play session. Thread-safe; all network work runs on one background thread.
class Client {
public:
    explicit Client(Config config);
    ~Client();  // Calls Shutdown()

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool SetSteamId(uint64_t steam_id64);
    bool SetSteamId(const std::string& steam_id64);

    void SetPaused(bool paused);

    // Session "data" (max 16 KB). Replaces the previous value; the server keeps the latest.
    bool SetSessionData(const JsonObject& data);
    bool SetSessionData(std::string json_object);

    // Events: queued (on disk with storage_dir), sent in order with an event_id, retried until
    // delivered. They wait for the SteamID; a crash before Steam starts goes out once it does.
    // The queue holds at most 1000 events and 1 MB; the oldest are dropped first, crashes last.

    // Goal without a target: done.
    bool Goal(const std::string& key);
    // Goal with a target: level reached. Repeated calls collapse to the highest pending value.
    bool Goal(const std::string& key, double value);
    // Both return false and send nothing while the server says the player is not accepted into
    // a playtest of this game (Stats::player_accepted == 0): such goals are never counted.

    bool TestResult(int test_id, bool passed, const std::string& message = {}, const std::string& reward = {});

    // Written to disk before returning, so it survives the process dying right after.
    // Same fingerprint while still queued is sent once. At most 20 crashes wait in the queue;
    // more return false without touching the disk, so a crash storm never stalls the caller.
    bool Crash(const std::string& fingerprint, const std::string& message = {}, const std::string& stack = {},
               const JsonObject& hardware = {});

    // Any event: TEST_RESULT, CRASH, GOAL_PROGRESS, GAME_STARTED, GAME_FINISHED.
    bool Event(const std::string& event_type, const std::string& status, const JsonObject& data);

    // Sends a heartbeat and queued events now (async).
    void Flush();

    Stats GetStats() const;

    // Final send (bounded), then opens exit_url if enabled. Idempotent.
    void Shutdown();

    struct State;

private:
    bool Enqueue(detail::QueuedEvent&& ev);

    std::shared_ptr<State> state_;
};

// http(s) only; desktop platforms. Engines may prefer their own (Application.OpenURL, LaunchURL, shell_open).
bool OpenUrl(const std::string& url);

}  // namespace ptf
