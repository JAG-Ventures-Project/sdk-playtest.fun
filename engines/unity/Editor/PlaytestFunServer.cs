// Editor-only read of the game's playtests and goals: GET /sdk/game/playtests with the game key.
// Replies are cached per game key in Library/PlaytestFun/.

using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Runtime.CompilerServices;
using UnityEditor;
using UnityEngine;
using UnityEngine.Networking;

[assembly: InternalsVisibleTo("PlaytestFun.Tests")]

namespace PlaytestFunSdk.Editor
{
    [Serializable]
    sealed class PlaytestsReply
    {
        public bool success;
        public Playtest[] data;
        public ApiError error;
    }

    [Serializable]
    sealed class ApiError
    {
        public string code, message;
    }

    [Serializable]
    sealed class Playtest
    {
        public string id, title, versionLabel, publishState, startsAt, endsAt;
        public Goal[] goals;
    }

    // The goal fields are read leniently: the first non-empty name wins.
    [Serializable]
    sealed class Goal
    {
        public string key, goalKey, slug, id, title, name, description;
        public double target, targetValue;

        public string Key => First(key, goalKey, slug, id);
        public string Title => First(title, name, Key);
        public double Target => target != 0 ? target : targetValue;

        static string First(params string[] values) => values.FirstOrDefault(v => !string.IsNullOrEmpty(v)) ?? "";
    }

    static class PlaytestFunServer
    {
        const string BaseUrl = "https://api.playtest.fun/v1";
        const string CacheDir = "Library/PlaytestFun";
        const string FetchedKey = "PlaytestFun.FetchedThisSession.";

        /// <summary>
        /// Last good reply for this game key. Kept in Library/ so it survives editor restarts but never
        /// reaches version control; one file per key so switching keys never shows another game's goals.
        /// </summary>
        internal static string RawJson(string gameKey)
        {
            var path = CachePath(gameKey);
            return path != null && File.Exists(path) ? File.ReadAllText(path) : "";
        }

        static string memoPath;
        static DateTime memoTime;
        static PlaytestsReply memo;

        /// <summary>The cached reply, parsed once per file write (the settings page asks on every repaint).</summary>
        internal static PlaytestsReply Cached(string gameKey)
        {
            var path = CachePath(gameKey);
            if (path == null || !File.Exists(path)) return null;
            var time = File.GetLastWriteTimeUtc(path);
            if (path != memoPath || time != memoTime)
            {
                memo = Parse(File.ReadAllText(path));
                memoPath = path;
                memoTime = time;
            }
            return memo;
        }

        /// <summary>When the cached reply was fetched, or null.</summary>
        internal static DateTime? FetchedAt(string gameKey)
        {
            var path = CachePath(gameKey);
            return path != null && File.Exists(path) ? File.GetLastWriteTime(path) : (DateTime?)null;
        }

        /// <summary>True once a fetch for this key was started in this editor session.</summary>
        internal static bool FetchedThisSession(string gameKey) =>
            SessionState.GetBool(FetchedKey + KeyHash(gameKey), false);

        internal static bool Busy { get; private set; }
        internal static string LastError { get; private set; } = "";

        internal static PlaytestsReply Parse(string json)
        {
            if (string.IsNullOrEmpty(json)) return null;
            try { return JsonUtility.FromJson<PlaytestsReply>(json); }
            catch (ArgumentException) { return null; }
        }

        internal static bool IsGameKey(string key) => (key ?? "").StartsWith("phub_gk_");

        /// <summary>Fetches the playtests; <paramref name="done"/> gets true when the cache was updated.</summary>
        internal static void Fetch(string gameKey, Action<bool> done)
        {
            if (Busy || !IsGameKey(gameKey)) { done?.Invoke(false); return; }
            Busy = true;
            LastError = "";
            SessionState.SetBool(FetchedKey + KeyHash(gameKey), true);
            var req = UnityWebRequest.Get(BaseUrl + "/sdk/game/playtests");
            req.SetRequestHeader("Authorization", "Bearer " + gameKey);
            req.timeout = 15;
            var op = req.SendWebRequest();
            op.completed += _ =>
            {
                bool ok = false;
                try
                {
                    var body = req.downloadHandler.text;
                    if (req.result == UnityWebRequest.Result.Success && Parse(body)?.data != null)
                    {
                        Directory.CreateDirectory(CacheDir);
                        File.WriteAllText(CachePath(gameKey), body);
                        ok = true;
                    }
                    else
                        LastError = Parse(body)?.error?.code is string code && code.Length > 0
                            ? $"HTTP {req.responseCode} {code}"
                            : $"HTTP {req.responseCode} {req.error}";
                }
                catch (Exception e)
                {
                    LastError = e.Message;
                }
                finally
                {
                    req.Dispose();
                    Busy = false;
                }
                done?.Invoke(ok);
            };
        }

        static string CachePath(string gameKey) =>
            IsGameKey(gameKey) ? $"{CacheDir}/playtests-{KeyHash(gameKey)}.json" : null;

        static string KeyHash(string gameKey)
        {
            using (var sha = SHA256.Create())
            {
                var hash = sha.ComputeHash(Encoding.UTF8.GetBytes(gameKey ?? ""));
                return BitConverter.ToString(hash, 0, 8).Replace("-", "").ToLowerInvariant();
            }
        }
    }
}
