// Project Settings > PLAYTEST.FUN. Edits Assets/Resources/PlaytestFunSettings.asset,
// creating it on first open.

using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEngine;

namespace PlaytestFunSdk.Editor
{
    public static class PlaytestFunSettingsProvider
    {
        public const string AssetPath = "Assets/Resources/" + PlaytestFunSettings.ResourceName + ".asset";

        /// <summary>The project's settings asset; created if missing (also usable from build scripts).</summary>
        public static PlaytestFunSettings GetOrCreateSettings()
        {
            var settings = PlaytestFunSettings.Load();  // any Resources folder
            if (settings != null) return settings;
            settings = ScriptableObject.CreateInstance<PlaytestFunSettings>();
            Directory.CreateDirectory(Path.GetDirectoryName(AssetPath));
            AssetDatabase.CreateAsset(settings, AssetPath);
            AssetDatabase.SaveAssets();
            return settings;
        }

        static SerializedObject serialized, options;

        [SettingsProvider]
        static SettingsProvider Create() => new SettingsProvider("Project/PLAYTEST.FUN", SettingsScope.Project)
        {
            label = "PLAYTEST.FUN",
            activateHandler = (_, __) =>
            {
                var settings = GetOrCreateSettings();
                serialized = new SerializedObject(settings);
                options = null;
                if (PlaytestFunEditorSettings.instance.fetchOnOpen && !PlaytestFunServer.FetchedThisSession(settings.gameKey))
                    PlaytestFunServer.Fetch(settings.gameKey, _ok => SettingsService.RepaintAllSettingsWindow());
            },
            guiHandler = _ => Draw(),
            keywords = new HashSet<string> { "playtest", "game key", "steam", "session", "goals", "namespace" },
        };

        static void Draw()
        {
            if (serialized == null || serialized.targetObject == null)
                serialized = new SerializedObject(GetOrCreateSettings());
            serialized.Update();

            var it = serialized.GetIterator();
            it.NextVisible(true);  // m_Script
            while (it.NextVisible(false)) EditorGUILayout.PropertyField(it, true);
            serialized.ApplyModifiedProperties();

            var key = ((PlaytestFunSettings)serialized.targetObject).gameKey ?? "";
            EditorGUILayout.Space();
            if (key.StartsWith("phub_sk_"))
                EditorGUILayout.HelpBox("This is a studio key. Never ship it: use the game key (phub_gk_...).", MessageType.Error);
            else if (key.Length == 0)
                EditorGUILayout.HelpBox("Paste the game key from playtest.fun/developers. Until then the SDK does not start.", MessageType.Warning);
            else if (!key.StartsWith("phub_gk_"))
                EditorGUILayout.HelpBox("Game keys start with phub_gk_.", MessageType.Warning);

            EditorGUILayout.LabelField("Stored in " + AssetDatabase.GetAssetPath(serialized.targetObject), EditorStyles.miniLabel);

            DrawPlaytests((PlaytestFunSettings)serialized.targetObject);
        }

        static bool showRaw;

        static void DrawPlaytests(PlaytestFunSettings settings)
        {
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Playtests and goals", EditorStyles.boldLabel);
            using (new EditorGUILayout.HorizontalScope())
            {
                using (new EditorGUI.DisabledScope(PlaytestFunServer.Busy || !PlaytestFunServer.IsGameKey(settings.gameKey)))
                {
                    if (GUILayout.Button(PlaytestFunServer.Busy ? "Fetching..." : "Fetch from server", GUILayout.Width(160)))
                        PlaytestFunServer.Fetch(settings.gameKey, _ => SettingsService.RepaintAllSettingsWindow());
                }
                if (PlaytestFunServer.FetchedAt(settings.gameKey) is System.DateTime at)
                    EditorGUILayout.LabelField("Fetched " + at.ToString("yyyy-MM-dd HH:mm"), EditorStyles.miniLabel);
            }
            if (PlaytestFunServer.LastError.Length > 0)
                EditorGUILayout.HelpBox("Fetch failed: " + PlaytestFunServer.LastError, MessageType.Error);

            DrawStaleness(settings);

            var playtests = PlaytestFunServer.Cached(settings.gameKey)?.data;
            if (playtests != null)
            {
                if (playtests.Length == 0)
                    EditorGUILayout.HelpBox("This game has no playtests yet.", MessageType.Info);
                foreach (var pt in playtests)
                    DrawPlaytest(settings, pt);
            }

            DrawCodegenOptions();

            if (playtests != null)
            {
                showRaw = EditorGUILayout.Foldout(showRaw, "Raw server reply");
                if (showRaw)
                    EditorGUILayout.TextArea(PlaytestFunServer.RawJson(settings.gameKey), EditorStyles.wordWrappedLabel);
            }
        }

        static void DrawPlaytest(PlaytestFunSettings settings, Playtest pt)
        {
            using (new EditorGUILayout.VerticalScope(EditorStyles.helpBox))
            {
                bool selected = settings.playtestId == pt.id;
                EditorGUILayout.LabelField(pt.title + (selected ? "  (Playtest Id)" : ""), EditorStyles.boldLabel);
                EditorGUILayout.SelectableLabel(pt.id, GUILayout.Height(EditorGUIUtility.singleLineHeight));
                EditorGUILayout.LabelField($"{pt.versionLabel} · {pt.publishState} · {Date(pt.startsAt)} – {Date(pt.endsAt)}", EditorStyles.miniLabel);

                var goals = pt.goals ?? new Goal[0];
                if (goals.Length == 0)
                    EditorGUILayout.LabelField("No goals yet. Add them on playtest.fun, then fetch again.", EditorStyles.miniLabel);
                foreach (var g in goals)
                    DrawGoal(g);
                if (goals.Length > 0 && goals.All(g => g.Target == 0))
                    EditorGUILayout.LabelField("No goal has a target on the server. Set targets on playtest.fun, then fetch again.", EditorStyles.miniLabel);

                using (new EditorGUILayout.HorizontalScope())
                {
                    using (new EditorGUI.DisabledScope(selected))
                    {
                        if (GUILayout.Button("Use as Playtest Id"))
                        {
                            Undo.RecordObject(settings, "Set PLAYTEST.FUN playtest");
                            settings.playtestId = pt.id;
                            EditorUtility.SetDirty(settings);
                            AssetDatabase.SaveAssets();
                        }
                    }
                    if (GUILayout.Button("Generate goal constants"))
                        Generate(pt);
                }
            }
        }

        static GUIStyle targetStyle;

        // key | title | target, the target right-aligned so the column lines up.
        static void DrawGoal(Goal g)
        {
            if (targetStyle == null)
                targetStyle = new GUIStyle(EditorStyles.label) { alignment = TextAnchor.MiddleRight };
            using (new EditorGUILayout.HorizontalScope())
            {
                EditorGUILayout.SelectableLabel(g.Key, GUILayout.Width(EditorGUIUtility.labelWidth),
                                                GUILayout.Height(EditorGUIUtility.singleLineHeight));
                EditorGUILayout.LabelField(g.Title, GUILayout.MinWidth(60));
                var target = g.Target != 0 ? "Target: " + g.Target.ToString("0.##", CultureInfo.InvariantCulture) : "no target";
                using (new EditorGUI.DisabledScope(g.Target == 0))
                    EditorGUILayout.LabelField(target, targetStyle, GUILayout.Width(110));
            }
        }

        static void DrawStaleness(PlaytestFunSettings settings)
        {
            var why = PlaytestFunGoalsMenu.Staleness(settings, out var regenerate);
            if (why == null) return;
            using (new EditorGUILayout.HorizontalScope())
            {
                EditorGUILayout.HelpBox(why, MessageType.Warning);
                using (new EditorGUI.DisabledScope(regenerate == null))
                {
                    if (GUILayout.Button("Generate again", GUILayout.Width(120), GUILayout.ExpandHeight(true)))
                        Generate(regenerate);
                }
            }
        }

        static void DrawCodegenOptions()
        {
            var opts = PlaytestFunEditorSettings.instance;
            if (options == null || options.targetObject == null) options = new SerializedObject(opts);
            options.Update();

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Goal constants", EditorStyles.boldLabel);
            EditorGUI.BeginChangeCheck();
            EditorGUILayout.PropertyField(options.FindProperty(nameof(opts.fetchOnOpen)));
            EditorGUILayout.PropertyField(options.FindProperty(nameof(opts.buildVersionFromPlaytest)), new GUIContent("Build Version From Playtest"));
            EditorGUILayout.PropertyField(options.FindProperty(nameof(opts.goalsPath)), new GUIContent("Output Path"));
            EditorGUILayout.PropertyField(options.FindProperty(nameof(opts.goalsNamespace)), new GUIContent("Namespace"));
            EditorGUILayout.PropertyField(options.FindProperty(nameof(opts.goalsClassName)), new GUIContent("Class Name"));
            if (EditorGUI.EndChangeCheck())
            {
                options.ApplyModifiedPropertiesWithoutUndo();
                opts.Save();
            }

            foreach (var error in new[]
                     {
                         PlaytestFunEditorSettings.ValidatePath(opts.goalsPath),
                         PlaytestFunEditorSettings.ValidateNamespace(opts.goalsNamespace),
                         PlaytestFunEditorSettings.ValidateClassName(opts.goalsClassName),
                     })
                if (error != null) EditorGUILayout.HelpBox(error + " The default is used until it is fixed.", MessageType.Error);

            EditorGUILayout.LabelField("Writes " + opts.OutputPath + "  ·  Tools > PLAYTEST.FUN > Fetch and Generate Goals", EditorStyles.miniLabel);
        }

        static void Generate(Playtest pt)
        {
            var error = PlaytestFunCodegen.Write(pt);
            if (error != null) EditorUtility.DisplayDialog("PLAYTEST.FUN", error, "OK");
        }

        static string Date(string iso) =>
            System.DateTime.TryParse(iso, null, System.Globalization.DateTimeStyles.RoundtripKind, out var d)
                ? d.ToLocalTime().ToString("yyyy-MM-dd HH:mm") : iso;
    }
}
