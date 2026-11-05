# PLAYTEST.FUN SDK for Unity

## Install from git

Window → Package Manager → **+** → *Add package from git URL*:

```
https://github.com/JAG-Ventures-Project/sdk-playtest.fun.git?path=/engines/unity#v1.5.0
```

Or add it to `Packages/manifest.json`:

```json
"fun.playtest.sdk": "https://github.com/JAG-Ventures-Project/sdk-playtest.fun.git?path=/engines/unity#v1.5.0"
```

Drop `#v1.5.0` to track `main`.

## Use

1. Open **Edit → Project Settings → PLAYTEST.FUN** and paste the game key. It is saved to
   `Assets/Resources/PlaytestFunSettings.asset`. The SDK starts before the first scene loads and lives
   across scenes, so no scene object is needed.
   - To start it later (e.g. after player consent), turn off *Start Automatically* and call
     `PlaytestFunSdk.PlaytestFun.StartFromSettings()`.
   - Adding the `PlaytestFun` component to a scene still works when *Start Automatically* is off.
     If the SDK is already running, a scene component is ignored with a warning.
   - Turn off *Run In Editor* so your own Play mode sessions in the Editor never reach playtest.fun.
     `PlaytestFun.Instance` still exists there, so game code needs no Editor checks; every call
     returns `false` and nothing is sent. Builds always run.
2. Once Steam is up, pass the player's id:
   ```csharp
   PlaytestFunSdk.PlaytestFun.Instance.SetSteamId(SteamUser.GetSteamID().m_SteamID);
   ```
3. Report goals (see [Goals](#goals)).

## Goals

### Generate the goal constants

Add the goals to the playtest on playtest.fun, then run **Tools → PLAYTEST.FUN → Fetch and Generate Goals**.
It writes `Assets/PlaytestFun/Goals.cs` for the *Playtest Id* (or the game's only playtest),
with one constant per goal key:

```csharp
namespace PlaytestFunSdk
{
    public static class Goals
    {
        public const string PlaytestId = "11111111-...";
        /// <summary>Version label of the playtest's build.</summary>
        public const string VersionLabel = "f1";

        /// <summary>Reach the beach</summary>
        public const string ReachTheBeach = "reach_the_beach";

        /// <summary>Kill 1000 monster (target 1000)</summary>
        public const string Kill1000Monster = "kill_1000_monster";
        public const double Kill1000MonsterTarget = 1000;
    }
}
```

- Run it again whenever the goals change. A removed or renamed goal then becomes a compile error,
  not a silently wrong key.
- Commit the generated file so the whole team uses the same constants. Do not edit it by hand.
- Project Settings → PLAYTEST.FUN lists each playtest and its goals, sets the *Playtest Id*, and warns
  when the generated file is out of date (with a *Generate again* button).
- Generating also sets *Build Version* in Project Settings → PLAYTEST.FUN to the playtest's version
  label (`f1` above), so session ids and crashes are tagged with the build under test. The settings
  page warns when the two differ. Turn off *Build Version From Playtest* to set it yourself.
- The output path, namespace and class name are options there, saved to
  `ProjectSettings/PlaytestFunEditorSettings.asset`. A file that PLAYTEST.FUN did not write is never
  overwritten.

### Send goals

A goal without a target: call it once, when the player reaches it.

```csharp
using PlaytestFunSdk;

void OnTutorialFinished() => PlaytestFun.Instance?.Goal(Goals.FinishTutorial);
```

A goal with a target: send the player's **current total**, not the increment. Calling it every frame
is fine; pending values collapse to the highest one, so no request storm.

```csharp
int monstersKilled;

void OnMonsterKilled()
{
    monstersKilled++;
    PlaytestFun.Instance?.Goal(Goals.Kill1000Monster, monstersKilled);
}
```

If the goal has no target on playtest.fun, send it once when it is reached instead:
`if (monstersKilled == 1000) PlaytestFun.Instance?.Goal(Goals.Kill1000Monster);`

### What happens to a goal

- `Goal` returns `true` when the goal is queued. It is written to disk and retried until delivered,
  so a goal reached offline or right before a crash still arrives.
- Before `SetSteamId`, goals wait in the queue and go out once the id is known.
- If the player is **not accepted** into a playtest of this game (or their Steam account is not linked
  on playtest.fun), goals are never counted. The SDK learns this from the server when the session
  starts; from then on `Goal` returns `false` and sends nothing, and goals queued before that are
  dropped without a request. Other events (test results, crashes) still go out. Play time
  heartbeats slow down to one every 5 minutes, so a player accepted mid-session is picked up.
- `false` is also returned when the SDK is not running: no game key, a rejected key, or the Editor
  with *Run In Editor* off. Use `?.` because `Instance` is null until the SDK has started.
- While playing in the Editor, the `PlaytestFun` object's Inspector shows *Accepted into playtest*,
  the events not sent yet, and the last HTTP status.

## Crash reports

Both are off by default; turn them on in Project Settings → PLAYTEST.FUN.

- **Report Exceptions**: every unhandled C# exception is sent as a `CRASH` event with its message
  and stack trace. The fingerprint is the exception line plus the top stack frame, so the same bug
  groups together, and an exception thrown every frame is queued only once. `Debug.LogError` is not sent.
- **Send Log After Crash** (builds only): catches what C# never sees — a native crash, a freeze
  ended from Task Manager, a power cut. While the game runs, a marker file sits in
  `Application.persistentDataPath/playtestfun/`; a normal quit removes it. When the next start finds it,
  the previous run's Player log (`Player-prev.log`) is sent as a `CRASH` event with the fingerprint
  `abnormal_exit`, the build version and the time that run started. A log over 20 KB keeps its last
  20 KB, where the crash is. On mobile the marker is removed while the app is paused, so the OS
  closing a backgrounded app is not reported. The log can contain hardware details and file paths:
  tell your testers before turning it on.

Report your own crashes with `PlaytestFun.Instance?.Crash("SaveCorrupt@LoadGame", message, stack)`.
Crash events are written to disk before the call returns, and they are sent for every player,
accepted or not.

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
