# PLAYTEST.FUN SDK for Unity

## Install from git

Window → Package Manager → **+** → *Add package from git URL*:

```
https://github.com/JAG-Ventures-Project/sdk-playtest.fun.git?path=/engines/unity#v1.0.1
```

Or add it to `Packages/manifest.json`:

```json
"fun.playtest.sdk": "https://github.com/JAG-Ventures-Project/sdk-playtest.fun.git?path=/engines/unity#v1.0.1"
```

Drop `#v1.0.1` to track `main`.

## Use

1. Add the `PlaytestFunSdk.PlaytestFun` component to a GameObject in your first scene and paste the game key.
2. Once Steam is up, pass the player's id:
   ```csharp
   PlaytestFunSdk.PlaytestFun.Instance.SetSteamId(SteamUser.GetSteamID().m_SteamID);
   ```

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
