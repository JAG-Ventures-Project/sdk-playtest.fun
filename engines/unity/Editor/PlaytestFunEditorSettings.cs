// Editor-only options for goal code generation, kept out of the shipped settings asset.
// Saved to ProjectSettings/PlaytestFunEditorSettings.asset so the whole team shares them.

using System.Text.RegularExpressions;
using UnityEditor;
using UnityEngine;

namespace PlaytestFunSdk.Editor
{
    [FilePath("ProjectSettings/PlaytestFunEditorSettings.asset", FilePathAttribute.Location.ProjectFolder)]
    sealed class PlaytestFunEditorSettings : ScriptableSingleton<PlaytestFunEditorSettings>
    {
        internal const string DefaultPath = "Assets/PlaytestFun/PlaytestFunGoals.cs";
        internal const string DefaultClassName = "PlaytestFunGoals";

        [Tooltip("Fetch playtests and goals once per editor session when this page opens.")]
        public bool fetchOnOpen = true;
        [Tooltip("Where the goal constants are written. Must be under Assets/ and end in .cs.")]
        public string goalsPath = DefaultPath;
        [Tooltip("Namespace of the generated class. Empty = global namespace.")]
        public string goalsNamespace = "";
        public string goalsClassName = DefaultClassName;
        [SerializeField, HideInInspector] internal string lastGeneratedPath = "";

        internal string OutputPath => ValidatePath(goalsPath) == null ? goalsPath : DefaultPath;
        internal string Namespace => ValidateNamespace(goalsNamespace) == null ? goalsNamespace ?? "" : "";
        internal string ClassName => ValidateClassName(goalsClassName) == null ? goalsClassName : DefaultClassName;

        internal void Save() => Save(true);

        static readonly Regex Ident = new Regex(@"^[A-Za-z_][A-Za-z0-9_]*$");

        /// <summary>Null when the path is usable, otherwise why not.</summary>
        internal static string ValidatePath(string path)
        {
            if (string.IsNullOrEmpty(path)) return "Path is empty.";
            if (!path.StartsWith("Assets/")) return "Path must start with Assets/.";
            if (!path.EndsWith(".cs")) return "Path must end with .cs.";
            if (path.Contains("..") || path.Contains("\\")) return "Use forward slashes and no '..'.";
            return null;
        }

        internal static string ValidateNamespace(string ns)
        {
            if (string.IsNullOrEmpty(ns)) return null;
            foreach (var part in ns.Split('.'))
                if (!Ident.IsMatch(part)) return $"'{ns}' is not a valid namespace.";
            return null;
        }

        internal static string ValidateClassName(string name) =>
            name != null && Ident.IsMatch(name) ? null : $"'{name}' is not a valid class name.";
    }
}
