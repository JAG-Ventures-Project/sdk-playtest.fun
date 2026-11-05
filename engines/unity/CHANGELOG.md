# Changelog

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
