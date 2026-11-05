// Tools > PLAYTEST.FUN: fetch the goals and regenerate the constants in one step.

using System.IO;
using UnityEditor;
using UnityEngine;

namespace PlaytestFunSdk.Editor
{
    static class PlaytestFunGoalsMenu
    {
        const string Root = "Tools/PLAYTEST.FUN/";
        const string Title = "PLAYTEST.FUN";

        [MenuItem(Root + "Fetch and Generate Goals")]
        static void FetchAndGenerate()
        {
            var settings = PlaytestFunSettingsProvider.GetOrCreateSettings();
            if (!PlaytestFunServer.IsGameKey(settings.gameKey))
            {
                EditorUtility.DisplayDialog(Title, "Paste the game key (phub_gk_...) in Project Settings > PLAYTEST.FUN first.", "Open settings");
                OpenSettings();
                return;
            }

            PlaytestFunServer.Fetch(settings.gameKey, ok =>
            {
                SettingsService.RepaintAllSettingsWindow();
                if (!ok)
                {
                    EditorUtility.DisplayDialog(Title, "Fetch failed: " + PlaytestFunServer.LastError, "OK");
                    return;
                }
                var pt = PlaytestFunCodegen.Pick(PlaytestFunServer.Cached(settings.gameKey).data, settings.playtestId, out var error);
                if (pt == null)
                {
                    EditorUtility.DisplayDialog(Title, error, "Open settings");
                    OpenSettings();
                    return;
                }
                error = PlaytestFunCodegen.Write(pt);
                if (error != null)
                    EditorUtility.DisplayDialog(Title, error, "OK");
                else
                    Debug.Log($"PLAYTEST.FUN: {pt.goals?.Length ?? 0} goal(s) of \"{pt.title}\" written to {PlaytestFunEditorSettings.instance.OutputPath}.");
            });
        }

        [MenuItem(Root + "Fetch and Generate Goals", true)]
        static bool CanFetch() => !PlaytestFunServer.Busy;

        [MenuItem(Root + "Project Settings")]
        static void OpenSettings() => SettingsService.OpenProjectSettings("Project/PLAYTEST.FUN");

        /// <summary>Why the generated goal constants are out of date, or null; see PlaytestFunCodegen.Staleness.</summary>
        internal static string Staleness(PlaytestFunSettings settings, out Playtest regenerate)
        {
            var opts = PlaytestFunEditorSettings.instance;
            var path = opts.OutputPath;
            var code = File.Exists(path) ? File.ReadAllText(path) : null;
            var old = opts.lastGeneratedPath;
            if (code == null && !string.IsNullOrEmpty(old) && old != path && File.Exists(old))
            {
                regenerate = PlaytestFunCodegen.Pick(PlaytestFunServer.Cached(settings.gameKey)?.data, settings.playtestId, out _);
                return $"The goal constants are still at {old}; the output path is now {path}.";
            }
            var playtests = PlaytestFunServer.Cached(settings.gameKey)?.data;
            var why = PlaytestFunCodegen.Staleness(code, playtests, settings.playtestId, opts.Namespace, opts.ClassName, out regenerate);
            if (why != null || code == null || !opts.buildVersionFromPlaytest) return why;

            // Constants are current: also check Build Version against their playtest.
            var id = PlaytestFunCodegen.ReadPlaytestId(code);
            var forFile = playtests == null ? null : System.Array.Find(playtests, p => p.id == id);
            why = PlaytestFunCodegen.BuildVersionMismatch(settings.buildVersion, forFile);
            if (why != null) regenerate = forFile;
            return why;
        }
    }
}
