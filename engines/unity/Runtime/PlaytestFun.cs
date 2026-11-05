// PLAYTEST.FUN native SDK for Unity (2021.3+).
// The package ships Runtime/Plugins/x86_64/playtestfun.dll; other platforms: see README.
//
// 1. Paste the game key in Project Settings > PLAYTEST.FUN. The SDK then starts before the
//    first scene loads. (Or turn that off and add this component to your first scene.)
// 2. Once Steam is up (Steamworks.NET shown):
//        PlaytestFunSdk.PlaytestFun.Instance.SetSteamId(SteamUser.GetSteamID().m_SteamID);
// Play time is measured natively and sent every minute and on quit; then playtest.fun opens.

using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;

[assembly: InternalsVisibleTo("PlaytestFun.Tests")]
[assembly: InternalsVisibleTo("PlaytestFun.PlayTests")]

namespace PlaytestFunSdk
{
    public sealed class PlaytestFun : MonoBehaviour
    {
        public static PlaytestFun Instance { get; private set; }

        [SerializeField] string gameKey = "phub_gk_YOUR_GAME_KEY";
        [Tooltip("Off: in the Editor nothing is sent (the calls below just return false). Builds always run.")]
        [SerializeField] bool runInEditor = true;
        [Tooltip("Needed only when a player is in more than one playtest of the game.")]
        [SerializeField] string playtestId = "";
        [Tooltip("Empty = Application.version. Prefixes the session id and tags crashes.")]
        [SerializeField] string buildVersion = "";
        [SerializeField] uint heartbeatSeconds = 60;
        [Tooltip("Send unhandled exceptions as CRASH events (one per fingerprint while queued).")]
        [SerializeField] bool reportExceptions = false;
        [SerializeField] bool openSiteOnQuit = true;
        [SerializeField] bool openSiteInEditor = false;
        [SerializeField] string exitUrl = "https://playtest.fun";

        public struct Stats
        {
            public double LocalSessionSeconds, ServerSessionSeconds, ServerTotalSeconds;
            public int LastHttpStatus, PlayerLinked, PlayerAccepted, PendingEvents;
            public string LastError, SessionId;
        }

        bool running;

        /// <summary>False when not configured, when the key was rejected at start, or in the Editor with Run In Editor off.</summary>
        public bool IsRunning => running;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        static void AutoStart()
        {
            var settings = PlaytestFunSettings.Load();
            if (settings != null && settings.startAutomatically) Bootstrap(settings);
        }

        /// <summary>
        /// Starts the SDK from Project Settings > PLAYTEST.FUN. Use it when Start Automatically is off,
        /// e.g. after the player consents. Returns the running instance, or null if not configured.
        /// </summary>
        public static PlaytestFun StartFromSettings()
        {
            var settings = PlaytestFunSettings.Load();
            if (settings != null) return Bootstrap(settings);
            Debug.LogWarning("playtest.fun: no settings yet, open Project Settings > PLAYTEST.FUN");
            return null;
        }

        /// <summary>Creates the SDK object from settings, unless one is already running.</summary>
        internal static PlaytestFun Bootstrap(PlaytestFunSettings settings)
        {
            if (Instance != null) return Instance;
            if (string.IsNullOrEmpty(settings.gameKey))
            {
                Debug.LogWarning("playtest.fun: set the game key in Project Settings > PLAYTEST.FUN");
                return null;
            }
            var go = new GameObject("PLAYTEST.FUN");
            go.SetActive(false);  // Awake waits until the settings are applied
            var sdk = go.AddComponent<PlaytestFun>();
            sdk.Apply(settings);
            go.SetActive(true);
            return Instance == sdk ? sdk : null;
        }

        void Apply(PlaytestFunSettings s)
        {
            gameKey = s.gameKey;
            runInEditor = s.runInEditor;
            playtestId = s.playtestId;
            buildVersion = s.buildVersion;
            heartbeatSeconds = s.heartbeatSeconds;
            reportExceptions = s.reportExceptions;
            openSiteOnQuit = s.openSiteOnQuit;
            openSiteInEditor = s.openSiteInEditor;
            exitUrl = s.exitUrl;
        }

        void Awake()
        {
            if (Instance != null)
            {
                Debug.LogWarning("playtest.fun: the SDK is already running (Project Settings or another component); this one is ignored", this);
                // Drop a dedicated object entirely, but never other components that share it.
                if (GetComponents<Component>().Length == 2) Destroy(gameObject); else Destroy(this);
                return;
            }
            Instance = this;
            DontDestroyOnLoad(gameObject);
            if (Application.isEditor && !runInEditor)
            {
                // Instance stays set, so game code calling it needs no Editor checks; every call returns false.
                Debug.Log("playtest.fun: not running in the Editor (Run In Editor is off); nothing is sent", this);
                return;
            }
            running = Init();
            if (running && reportExceptions) Application.logMessageReceivedThreaded += OnUnityLog;
        }

        public bool SetSteamId(ulong steamId64) => running && Native.ptf_set_steam_id(steamId64) == 1;
        public bool SetSteamId(string steamId64) => running && Native.ptf_set_steam_id_str(steamId64) == 1;

        /// <summary>Free-form session data as a JSON object, max 16 KB.</summary>
        public bool SetSessionData(string jsonObject) => running && Native.ptf_set_session_data(jsonObject) == 1;

        // Events are queued on disk and retried until delivered.

        /// <summary>Goal without a target: done.</summary>
        public bool Goal(string key) => running && Native.ptf_goal(key) == 1;

        /// <summary>Goal with a target; calling it every frame is fine, pending values collapse.</summary>
        public bool Goal(string key, double value) => running && Native.ptf_goal_value(key, value) == 1;

        public bool TestResult(int testId, bool passed, string message = null) =>
            running && Native.ptf_test_result(testId, passed ? 1 : 0, message) == 1;

        public bool Crash(string fingerprint, string message = null, string stack = null) =>
            running && Native.ptf_crash(fingerprint, message, stack) == 1;

        /// <summary>Any event type; dataJson is a JSON object.</summary>
        public bool Event(string eventType, string status, string dataJson) =>
            running && Native.ptf_event(eventType, status, dataJson) == 1;

        public void Flush() { if (running) Native.ptf_flush(); }

        public Stats GetStats()
        {
            Native.ptf_get_stats(out var s);
            return new Stats
            {
                LocalSessionSeconds = s.local_session_seconds,
                ServerSessionSeconds = s.server_session_seconds,
                ServerTotalSeconds = s.server_total_seconds,
                LastHttpStatus = s.last_http_status,
                PlayerLinked = s.player_linked,
                PlayerAccepted = s.player_accepted,
                PendingEvents = s.pending_events,
                LastError = CString(s.last_error),
                SessionId = CString(s.session_id),
            };
        }

        void OnApplicationPause(bool paused) { if (running) Native.ptf_set_paused(paused ? 1 : 0); }

        // Any thread. Fingerprint = exception line + top frame, stable across runs.
        void OnUnityLog(string condition, string stackTrace, LogType type)
        {
            if (type != LogType.Exception || !running) return;
            var frame = stackTrace ?? "";
            int nl = frame.IndexOf('\n');
            if (nl >= 0) frame = frame.Substring(0, nl);
            Native.ptf_crash(condition + "@" + frame.Trim(), condition, stackTrace);
        }

        void OnApplicationQuit()
        {
            Application.logMessageReceivedThreaded -= OnUnityLog;
            if (!running) return;
            running = false;
            Native.ptf_shutdown(); // bounded final send
            if (openSiteOnQuit && (!Application.isEditor || openSiteInEditor)) Application.OpenURL(exitUrl);
        }

        void OnDestroy()
        {
            if (Instance != this) return;
            Application.logMessageReceivedThreaded -= OnUnityLog;
            if (running) { running = false; Native.ptf_shutdown(); }
            Instance = null;
        }

        bool Init()
        {
            var cfg = new Native.Config();
            Native.ptf_config_init(ref cfg);
            var strings = new IntPtr[4];
            try
            {
                cfg.game_key = strings[0] = Utf8(gameKey);
                cfg.playtest_id = strings[1] = Utf8(playtestId);
                cfg.storage_dir = strings[2] = Utf8(Application.persistentDataPath + "/playtestfun");
                cfg.build_version = strings[3] = Utf8(string.IsNullOrEmpty(buildVersion) ? Application.version : buildVersion);
                cfg.open_url_on_shutdown = 0; // Application.OpenURL works on every platform
                cfg.heartbeat_seconds = heartbeatSeconds;
                cfg.log = Marshal.GetFunctionPointerForDelegate(LogCallback);
                return Native.ptf_init(ref cfg) == 1;
            }
            catch (DllNotFoundException e)
            {
                Debug.LogError("playtest.fun: native plugin missing: " + e.Message);
                return false;
            }
            finally
            {
                foreach (var p in strings) if (p != IntPtr.Zero) Marshal.FreeHGlobal(p);
            }
        }

        // Kept in a static field so the GC never collects it while native code holds it.
        static readonly Native.LogFn LogCallback = OnLog;

        [AOT.MonoPInvokeCallback(typeof(Native.LogFn))]
        static void OnLog(int level, IntPtr message, IntPtr user)
        {
            var text = "playtest.fun: " + Marshal.PtrToStringAnsi(message);
            if (level >= 3) Debug.LogError(text);
            else if (level == 2) Debug.LogWarning(text);
            else Debug.Log(text);
        }

        internal static IntPtr Utf8(string s)
        {
            if (string.IsNullOrEmpty(s)) return IntPtr.Zero;
            var bytes = Encoding.UTF8.GetBytes(s);
            var p = Marshal.AllocHGlobal(bytes.Length + 1);
            Marshal.Copy(bytes, 0, p, bytes.Length);
            Marshal.WriteByte(p, bytes.Length, 0);
            return p;
        }

        static string CString(byte[] b)
        {
            if (b == null) return "";  // not filled in when the SDK is not running
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }

        internal static class Native
        {
#if UNITY_IOS && !UNITY_EDITOR
            const string Lib = "__Internal";
#else
            const string Lib = "playtestfun";
#endif
            [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
            public delegate void LogFn(int level, IntPtr message, IntPtr user);

            [StructLayout(LayoutKind.Sequential)]
            public struct Config
            {
                public uint struct_size;
                public IntPtr game_key, playtest_id, base_url, storage_dir, exit_url;
                public int open_url_on_shutdown;
                public uint heartbeat_seconds, shutdown_timeout_ms;
                public IntPtr log, log_user, http, http_user;
                public IntPtr build_version;
            }

            [StructLayout(LayoutKind.Sequential)]
            public struct StatsC
            {
                public double local_session_seconds, server_session_seconds, server_total_seconds;
                public int last_http_status, player_linked, player_accepted;
                [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)] public byte[] last_error;
                [MarshalAs(UnmanagedType.ByValArray, SizeConst = 80)] public byte[] session_id;
                public int pending_events;
            }

            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void ptf_config_init(ref Config c);
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int ptf_init(ref Config c);
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int ptf_set_steam_id(ulong id);
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int ptf_set_steam_id_str([MarshalAs(UnmanagedType.LPStr)] string id);
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void ptf_set_paused(int paused);
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] static extern int ptf_set_session_data(IntPtr json);
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void ptf_flush();
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern int ptf_get_stats(out StatsC s);
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern void ptf_shutdown();
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr ptf_version();

            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] static extern int ptf_goal(IntPtr key);
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] static extern int ptf_goal_value(IntPtr key, double value);
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] static extern int ptf_test_result(int id, int passed, IntPtr message);
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] static extern int ptf_crash(IntPtr fingerprint, IntPtr message, IntPtr stack);
            [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] static extern int ptf_event(IntPtr type, IntPtr status, IntPtr data);

            // Strings go out as UTF-8 (the default marshaller uses the ANSI code page on Windows).
            public static int ptf_set_session_data(string json) => WithUtf8(json, null, null, (a, _, __) => ptf_set_session_data(a));
            public static int ptf_goal(string key) => WithUtf8(key, null, null, (a, _, __) => ptf_goal(a));
            public static int ptf_goal_value(string key, double value) => WithUtf8(key, null, null, (a, _, __) => ptf_goal_value(a, value));
            public static int ptf_test_result(int id, int passed, string message) => WithUtf8(message, null, null, (a, _, __) => ptf_test_result(id, passed, a));
            public static int ptf_crash(string fingerprint, string message, string stack) => WithUtf8(fingerprint, message, stack, ptf_crash);
            public static int ptf_event(string type, string status, string data) => WithUtf8(type, status, data, ptf_event);

            static int WithUtf8(string a, string b, string c, Func<IntPtr, IntPtr, IntPtr, int> call)
            {
                IntPtr pa = Utf8(a), pb = Utf8(b), pc = Utf8(c);
                try { return call(pa, pb, pc); }
                finally
                {
                    if (pa != IntPtr.Zero) Marshal.FreeHGlobal(pa);
                    if (pb != IntPtr.Zero) Marshal.FreeHGlobal(pb);
                    if (pc != IntPtr.Zero) Marshal.FreeHGlobal(pc);
                }
            }
        }
    }
}
