// Project Settings > PLAYTEST.FUN. Edits Assets/Resources/PlaytestFunSettings.asset,
// creating it on first open.

using System.Collections.Generic;
using System.IO;
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

        static SerializedObject serialized;

        [SettingsProvider]
        static SettingsProvider Create() => new SettingsProvider("Project/PLAYTEST.FUN", SettingsScope.Project)
        {
            label = "PLAYTEST.FUN",
            activateHandler = (_, __) => serialized = new SerializedObject(GetOrCreateSettings()),
            guiHandler = _ => Draw(),
            keywords = new HashSet<string> { "playtest", "game key", "steam", "session" },
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
        }
    }
}
