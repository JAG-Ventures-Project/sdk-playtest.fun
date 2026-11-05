// Project-wide SDK settings, edited in Project Settings > PLAYTEST.FUN.
// Stored in a Resources folder so the player build can load it before the first scene.

using UnityEngine;

namespace PlaytestFunSdk
{
    public sealed class PlaytestFunSettings : ScriptableObject
    {
        public const string ResourceName = "PlaytestFunSettings";

        [Tooltip("Start the SDK before the first scene loads. Off: add the PlaytestFun component to a scene yourself.")]
        public bool startAutomatically = true;
        [Tooltip("Off: in the Editor the SDK object exists but sends nothing, so your own test runs never reach playtest.fun. Builds always run.")]
        public bool runInEditor = true;
        [Tooltip("phub_gk_... game key. Never a studio key.")]
        public string gameKey = "";
        [Tooltip("Needed only when a player is in more than one playtest of the game.")]
        public string playtestId = "";
        [Tooltip("Empty = Application.version. Prefixes the session id and tags crashes.")]
        public string buildVersion = "";
        public uint heartbeatSeconds = 60;
        [Tooltip("Send unhandled exceptions as CRASH events (one per fingerprint while queued).")]
        public bool reportExceptions = false;
        [Tooltip("Builds only. When a run ends without quitting (native crash, freeze, killed process), the next start sends the end of its Player log as a CRASH event. The log can contain hardware details and file paths.")]
        public bool sendLogAfterCrash = false;
        public bool openSiteOnQuit = true;
        public bool openSiteInEditor = false;
        public string exitUrl = "https://playtest.fun";

        public static PlaytestFunSettings Load() => Resources.Load<PlaytestFunSettings>(ResourceName);
    }
}
