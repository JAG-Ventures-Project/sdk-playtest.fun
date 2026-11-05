// Live session stats on the PlaytestFun object while playing.

using System;
using UnityEditor;
using UnityEngine;

namespace PlaytestFunSdk.Editor
{
    [CustomEditor(typeof(PlaytestFun))]
    sealed class PlaytestFunInspector : UnityEditor.Editor
    {
        public override bool RequiresConstantRepaint() => Application.isPlaying;

        public override void OnInspectorGUI()
        {
            var sdk = (PlaytestFun)target;
            if (Application.isPlaying && sdk == PlaytestFun.Instance && !sdk.IsRunning)
            {
                EditorGUILayout.HelpBox("Not running: nothing is sent. In the Editor this is usually Run In Editor being off.", MessageType.Info);
                EditorGUILayout.Space();
            }
            else if (Application.isPlaying && sdk == PlaytestFun.Instance)
            {
                DrawStats(sdk.GetStats());
                if (GUILayout.Button("Send now")) sdk.Flush();
                EditorGUILayout.Space();
            }

            using (new EditorGUI.DisabledScope(Application.isPlaying))
                DrawDefaultInspector();
            if (PlaytestFunSettings.Load() is PlaytestFunSettings s && s.startAutomatically)
                EditorGUILayout.HelpBox("Started from Project Settings > PLAYTEST.FUN; edit the values there.", MessageType.None);
        }

        static void DrawStats(PlaytestFun.Stats s)
        {
            EditorGUILayout.LabelField("Session", EditorStyles.boldLabel);
            Row("Play time (this device)", Duration(s.LocalSessionSeconds));
            Row("Play time (server, session)", Duration(s.ServerSessionSeconds));
            Row("Play time (server, total)", Duration(s.ServerTotalSeconds));
            Row("Session id", s.SessionId);
            Row("Steam account linked", YesNo(s.PlayerLinked));
            Row("Accepted into playtest", YesNo(s.PlayerAccepted));
            Row("Last HTTP status", s.LastHttpStatus == 0 ? "-" : s.LastHttpStatus.ToString());
            if (!string.IsNullOrEmpty(s.LastError)) Row("Last error", s.LastError);
            Row("Events not sent yet", s.PendingEvents.ToString());
        }

        static void Row(string label, string value) => EditorGUILayout.LabelField(label, value);

        static string Duration(double seconds) =>
            TimeSpan.FromSeconds(Math.Max(0, seconds)).ToString(seconds >= 3600 ? @"h\:mm\:ss" : @"m\:ss");

        static string YesNo(int v) => v < 0 ? "not known yet" : v == 1 ? "yes" : "no";
    }
}
