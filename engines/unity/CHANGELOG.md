# Changelog

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
