# PLAYTEST.FUN C++ SDK

A cross-platform C++17 core that reports a player's session play time to [playtest.fun](https://playtest.fun/developers), using their SteamID64. When the game closes, it opens playtest.fun in the player's browser. The same core runs in Unity, Unreal, Godot and custom engines.

- Play time is measured natively with a monotonic clock, so no per-frame tick is needed.
- All network work runs on one background thread. The game thread only takes a mutex for a few microseconds.
- A heartbeat goes out every 60 s with ±10 % jitter. This spreads the load, because the rate limit is 1200 req/min per game key, shared by all players. A final send with `ended_at` goes out on shutdown, bounded by `shutdown_timeout_ms`. A hung network never blocks exit.
- Retries back off on 5xx and network errors. On a 429, the SDK waits for the `retryAfterSeconds` the server sends.
- If a heartbeat fails, the session is written to `storage_dir`. The next launch sends it again with the same `session_id`, and the server never counts it twice.
- An invalid or revoked key (401) turns reporting off for the run, so the SDK stops spamming the API.
- No dependencies: WinHTTP on Windows, NSURLSession on macOS/iOS, libcurl on Linux. You can inject your own transport for consoles, Android or engine HTTP.

```
include/playtestfun/
  playtestfun.h     C API (stable ABI: Unity P/Invoke, FFI)
  playtestfun.hpp   C++ API (ptf::Client)
  transport.hpp     HTTP interface (inject your own)
  steam.hpp         optional Steamworks helper
src/                core + platform transports (each file is #if-guarded, so you can glob them all)
engines/unity       C# component over the C API
engines/unreal      UE5 plugin (GameInstanceSubsystem + Project Settings)
engines/godot       Godot 4 GDExtension node
examples/           custom engine sample
tests/              offline tests with a mock transport
```

## Custom engine (C++)

```cpp
#include "playtestfun/playtestfun.hpp"

ptf::Config cfg;
cfg.game_key      = "phub_gk_...";             // game key only, never a studio key
cfg.build_version = "0.8.4a";                  // session id "0.8.4a-20261104T180211Z-<random>", crash buildVersion
cfg.storage_dir   = "<save dir>/playtestfun";  // optional offline queue
ptf::Client client(cfg);

client.SetSteamId(SteamUser()->GetSteamID().ConvertToUint64());  // or ptf::SetSteamIdFromSteamworks(client)
client.SetPaused(true / false);                                  // app background / foreground

// Session data: the latest object is kept (max 16 KB). JsonObject escapes strings for you.
client.SetSessionData(ptf::JsonObject().Set("level", 3).Set("difficulty", "hard"));

// Events: queued on disk, sent in order with an event_id, retried until delivered.
client.Goal("collect_points", 120);  // goal with a target; every-frame calls collapse
client.Goal("finish_tutorial");      // goal without a target
// Goals return false and send nothing once the server says the player is not accepted (never counted).
client.TestResult(12, true, "Finished the tutorial");
client.Crash("NullRef@Render", "NullReferenceException", stack,
             ptf::JsonObject().Set("gpu", "RTX 3060"));  // on disk before it returns
client.Event("GAME_FINISHED", "DONE", ptf::JsonObject().Set("ending", "b"));

ptf::Stats s = client.GetStats();  // session_id, local_session_seconds, server_total_seconds, pending_events ...

client.Shutdown();  // final send, then opens https://playtest.fun (also runs from the destructor)
```

Build:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This builds three targets: `playtestfun` (static, C++ API), `playtestfun_shared` (C API: `.dll`, `.so`, or `.bundle` on macOS) and `ptf_custom_engine`.

## Unity

1. In Package Manager, choose **+** → *Add package from git URL* and paste:
   ```
   https://github.com/JAG-Ventures-Project/sdk-playtest.fun.git?path=/engines/unity#v1.3.0
   ```
   The package includes the Windows x64 plugin. For other platforms, run `cmake -P tools/build_unity.cmake` on that platform. It builds the plugin with the static CRT and copies it into `engines/unity/Runtime/Plugins/`. See [engines/unity/README.md](engines/unity/README.md).
2. Open Project Settings → PLAYTEST.FUN and paste the game key. The SDK starts before the first scene loads, so no scene object is needed.
   To start it yourself (e.g. after player consent), turn off *Start Automatically* and call `PlaytestFunSdk.PlaytestFun.StartFromSettings()`.
3. Once Steam is up, pass the player's id:
   ```csharp
   PlaytestFunSdk.PlaytestFun.Instance.SetSteamId(SteamUser.GetSteamID().m_SteamID);
   ```

On quit, the native SDK makes its final send, then `Application.OpenURL` opens the site. The site does not open in the Editor unless you enable that option.

## Unreal Engine 5

```sh
cmake -P tools/sync_unreal.cmake   # copies include/ + src/ into the plugin
```

1. Copy `engines/unreal/PlaytestFun` into `<Project>/Plugins/`.
2. Set the game key under Project Settings → Plugins → PLAYTEST.FUN.

The SteamID64 is read automatically from OnlineSubsystemSteam. You can also call `SetSteamId` from Blueprint or C++:

```cpp
GetGameInstance()->GetSubsystem<UPlaytestFunSubsystem>()->SetSteamId(Id);
```

On exit, the plugin makes its final send, then `FPlatformProcess::LaunchURL` opens the site.

## Godot 4

```sh
cd engines/godot
git clone -b 4.3 https://github.com/godotengine/godot-cpp
scons platform=windows   # or linux / macos
```

1. Copy `engines/godot/addons/playtestfun` into your project.
2. Add a `PlaytestFun` node as an autoload and set `game_key`.
3. Pass the player's id with GodotSteam:
   ```gdscript
   PlaytestFun.set_steam_id(str(Steam.getSteamID()))
   ```

The node shuts down on window close or `get_tree().quit()`, then `OS.shell_open` opens the site.

## Platforms without a built-in transport

Consoles and Android have no built-in transport. Implement `ptf::Transport`, or the C hook `ptf_config.http`, on top of your platform's or engine's HTTP and pass it in the config. Requests are blocking, and the SDK calls them only from its worker thread.

## Tests

The tests run offline against a mock transport. Compile `tests/tests.cpp` together with `src/*.cpp`, with `include/` and `src/` on the include path.
