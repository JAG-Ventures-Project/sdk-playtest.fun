# Changelog

## [1.5.2] - 2026-11-05

- Personal data is removed from crash text before it is queued (the Player log from *Send Log After
  Crash*, exceptions from *Report Exceptions*, and `Crash` calls): file paths keep only the file name;
  user, computer and device names, SteamIDs and e-mail addresses are replaced. Hardware and driver
  lines stay. Fingerprints no longer carry paths, so the same crash groups across players.

## [1.5.1] - 2026-11-05

- The event queue is capped at 1 MB as well as 1000 events (oldest dropped first, crashes last), and
  at 20 crashes. Past that, `Crash` returns `false` without writing the file, so a storm of distinct
  exceptions with *Report Exceptions* on no longer rewrites a growing queue file on the main thread.
  Native core 1.4.0 (Windows plugin rebuilt).

## [1.5.0] - 2026-11-05

- *Send Log After Crash* setting (builds only, off by default): when a run ends without quitting
  (native crash, freeze, killed process), the next start sends that run's Player log as a `CRASH`
  event with the fingerprint `abnormal_exit`. Logs over 20 KB keep their last 20 KB.
- README: a Crash reports section.

## [1.4.0] - 2026-11-05

- Generating the goal constants also sets *Build Version* to the playtest's version label, and the
  class gets a `VersionLabel` constant. The settings page warns when Build Version differs; turn off
  *Build Version From Playtest* to set it yourself.
- The goal constants are the `PlaytestFunSdk.Goals` class in `Assets/PlaytestFun/Goals.cs` by default.
  Projects that already saved generation options keep theirs.

## [1.3.0] - 2026-11-05

- Goals of a player who is not accepted into a playtest of the game (or not linked) are no longer sent:
  `Goal` returns `false`, and goals queued before the player check came back are dropped without a request.
  The player is checked before the first goal goes out. Native core 1.3.0 (Windows plugin rebuilt).
- *Run In Editor* setting: off, the SDK sends nothing from Editor Play mode. `PlaytestFun.Instance` still
  exists and every call returns `false`. Builds always run.
- `PlaytestFun.IsRunning`.
- Project Settings > PLAYTEST.FUN lists each goal as key, title and target ("no target" when the server sends none).
- README: a Goals section on generating the constants, sending goals and what happens to them.

## [1.2.2] - 2026-11-05

- Goal constants are generated in the `PlaytestFunSdk` namespace by default. Projects that already saved
  generation options keep theirs; an empty namespace still means the global namespace.

## [1.2.1] - 2026-11-05

- Goal titles are read from the server's `label` field, so the settings page and the generated
  constants show "Kill 1000 monster" instead of the key.

## [1.2.0] - 2026-11-05

- Project Settings > PLAYTEST.FUN lists the game's playtests and goals from the server and can set the Playtest Id.
- Goal constants: generate a C# class with one constant per goal key (output path, namespace and class name
  are options, saved to `ProjectSettings/PlaytestFunEditorSettings.asset`).
- *Tools > PLAYTEST.FUN > Fetch and Generate Goals* fetches and regenerates in one step.
- The fetched playtests are cached per game key in `Library/PlaytestFun/` and fetched once per editor
  session when the settings page opens.
- The settings page warns when the generated constants are out of date, with a *Generate again* button.
- Live session stats and a *Send now* button on the `PlaytestFun` component while playing.

## [1.1.0] - 2026-11-04

- Project Settings > PLAYTEST.FUN: settings live in `Assets/Resources/PlaytestFunSettings.asset`.
- The SDK starts before the first scene loads; no scene object needed.
- `PlaytestFun.StartFromSettings()` to start it yourself when *Start Automatically* is off.
- A duplicate scene component only removes itself, never other components on its object.
- Play mode tests for starting from settings.

## [1.0.1] - 2026-11-04

- Installable from git: the package now has its .meta files.
- Windows x64 plugin included (static CRT, no redistributable needed).
- Offline EditMode tests with a mock HTTP callback.

## [1.0.0] - 2026-11-04

- First release: `PlaytestFun` component over the native C API.
