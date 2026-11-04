// Minimal custom-engine integration.
// Usage: custom_engine <phub_gk_key> <steam_id64> [seconds]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "playtestfun/playtestfun.hpp"
// With Steamworks: #include <steam/steam_api.h> + #include "playtestfun/steam.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: %s <game_key> <steam_id64> [seconds]\n", argv[0]);
        return 1;
    }
    const int seconds = argc > 3 ? std::atoi(argv[3]) : 5;

    ptf::Config cfg;
    cfg.game_key = argv[1];
    cfg.build_version = "0.1.0";  // session id: 0.1.0-<utc>-<random>
    cfg.storage_dir = "playtestfun_data";
    cfg.heartbeat_seconds = 60;
    cfg.log = [](ptf::LogLevel level, const std::string& msg) {
        std::printf("[ptf:%d] %s\n", static_cast<int>(level), msg.c_str());
    };

    ptf::Client client(cfg);
    if (!client.SetSteamId(std::string(argv[2]))) {  // or ptf::SetSteamIdFromSteamworks(client)
        std::printf("invalid SteamID64\n");
        return 1;
    }
    client.SetSessionData(ptf::JsonObject().Set("level", 1).Set("difficulty", "normal"));
    client.TestResult(1, true, "Reached the main menu");

    // Game loop stand-in.
    for (int i = 0; i < seconds; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        client.Goal("collect_points", i * 10.0);  // fine every frame: pending values collapse
        const ptf::Stats s = client.GetStats();
        std::printf("local %.1fs | server session %.1fs total %.1fs | http %d %s | accepted %d | events %zu\n",
                    s.local_session_seconds, s.server_session_seconds, s.server_total_seconds, s.last_http_status,
                    s.last_error.c_str(), s.player_accepted, s.pending_events);
    }
    client.Goal("finish_tutorial");

    client.Shutdown();  // final send with ended_at, then opens https://playtest.fun
    return 0;
}
