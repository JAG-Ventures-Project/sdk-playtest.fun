# PLAYTEST.FUN SDK for Unity

## Install from git

Window → Package Manager → **+** → *Add package from git URL*:

```
https://github.com/JAG-Ventures-Project/sdk-playtest.fun.git?path=/engines/unity#v1.2.1
```

Or add it to `Packages/manifest.json`:

```json
"fun.playtest.sdk": "https://github.com/JAG-Ventures-Project/sdk-playtest.fun.git?path=/engines/unity#v1.2.1"
```

Drop `#v1.2.1` to track `main`.

## Use

1. Open **Edit → Project Settings → PLAYTEST.FUN** and paste the game key. It is saved to
   `Assets/Resources/PlaytestFunSettings.asset`. The SDK starts before the first scene loads and lives
   across scenes, so no scene object is needed.
   - To start it later (e.g. after player consent), turn off *Start Automatically* and call
     `PlaytestFunSdk.PlaytestFun.StartFromSettings()`.
   - Adding the `PlaytestFun` component to a scene still works when *Start Automatically* is off.
     If the SDK is already running, a scene component is ignored with a warning.
2. Once Steam is up, pass the player's id:
   ```csharp
   PlaytestFunSdk.PlaytestFun.Instance.SetSteamId(SteamUser.GetSteamID().m_SteamID);
   ```

3. Report goals with constants instead of hand-typed keys. Add the goals on playtest.fun, then run
   **Tools → PLAYTEST.FUN → Fetch and Generate Goals**. It writes `Assets/PlaytestFun/PlaytestFunGoals.cs`
   for the *Playtest Id* (or the game's only playtest):
   ```csharp
   PlaytestFunSdk.PlaytestFun.Instance.Goal(PlaytestFunGoals.FinishTutorial);
   PlaytestFunSdk.PlaytestFun.Instance.Goal(PlaytestFunGoals.CollectPoints, score);
   ```
   Run it again whenever the goals change. A removed goal then becomes a compile error, not a silently wrong key.
   Project Settings → PLAYTEST.FUN shows each playtest's goals, warns when the generated file is out of date,
   and sets the output path, namespace and class name (saved to `ProjectSettings/PlaytestFunEditorSettings.asset`).
   The generated file is meant to be committed; it is never overwritten unless PLAYTEST.FUN wrote it.

## Platforms

| Platform      | Plugin                                     |
| ------------- | ------------------------------------------ |
| Windows x64   | `Runtime/Plugins/x86_64/playtestfun.dll` (included) |
| macOS         | `Runtime/Plugins/macOS/playtestfun.bundle` |
| Linux x64     | `Runtime/Plugins/Linux/x86_64/libplaytestfun.so` |
| iOS           | static lib, called through `__Internal`    |

Build the plugin for the host platform with `cmake -P tools/build_unity.cmake` from the repo root.

## Tests

The package tests run in the Editor and never touch the network. Add the package to `testables` in `Packages/manifest.json`:

```json
"testables": ["fun.playtest.sdk"]
```

Then run them from Window → General → Test Runner → EditMode.
