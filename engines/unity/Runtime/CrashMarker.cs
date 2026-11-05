// Detects a previous run that ended without quitting (native crash, hang killed from Task Manager,
// power loss): a marker file exists while the game runs and is removed on a normal quit. When the
// next run finds it, the end of the previous run's Player log is sent as a CRASH event.

using System;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace PlaytestFunSdk
{
    static class CrashMarker
    {
        public const string Fingerprint = "abnormal_exit";

        /// <summary>The native core keeps the first 20000 bytes of a stack; the log tail stays under that.</summary>
        public const int MaxLogBytes = 20000;

        /// <summary>How much of the log end is read before the noise is stripped and it is cut to MaxLogBytes.</summary>
        public const int ReadBytes = 512 * 1024;

        // Unity's Windows crash report lists every loaded DLL (~200 lines, ~40 KB: installed software,
        // full paths) and symbol lookup noise. Dropped so the game's last lines and the stack fit.
        static readonly Regex Noise = new Regex(
            @"^(?:.*, SymType: '.*|SymInit: .*|\s*ERROR: SymGetSymFromAddr64.*)(?:\r?\n|$)",
            RegexOptions.Compiled | RegexOptions.Multiline | RegexOptions.CultureInvariant);

        public static string StripNoise(string log) => string.IsNullOrEmpty(log) ? log ?? "" : Noise.Replace(log, "");

        /// <summary>The last <paramref name="maxBytes"/> UTF-8 bytes of the text, starting at a whole line.</summary>
        public static string TailBytes(string text, int maxBytes = MaxLogBytes)
        {
            if (string.IsNullOrEmpty(text) || Encoding.UTF8.GetByteCount(text) <= maxBytes) return text ?? "";
            var bytes = Encoding.UTF8.GetBytes(text);
            int start = bytes.Length - maxBytes;
            int nl = Array.IndexOf(bytes, (byte)'\n', start);
            int from = nl >= 0 ? nl + 1 : start;
            while (from < bytes.Length && (bytes[from] & 0xC0) == 0x80) ++from;  // never mid-character
            return Encoding.UTF8.GetString(bytes, from, bytes.Length - from);
        }

        /// <summary>What the previous run left behind, or null if it quit normally (or never ran).</summary>
        public sealed class PreviousRun
        {
            public string BuildVersion = "", StartedUtc = "";

            public string Message
            {
                get
                {
                    var msg = "The previous session ended without quitting (crash, freeze or killed process)";
                    if (BuildVersion.Length > 0) msg += ", build " + BuildVersion;
                    if (StartedUtc.Length > 0) msg += ", started " + StartedUtc;
                    return msg + ".";
                }
            }
        }

        public static PreviousRun Read(string markerPath)
        {
            try
            {
                if (!File.Exists(markerPath)) return null;
                var lines = File.ReadAllLines(markerPath);
                return new PreviousRun
                {
                    BuildVersion = lines.Length > 0 ? lines[0].Trim() : "",
                    StartedUtc = lines.Length > 1 ? lines[1].Trim() : "",
                };
            }
            catch (Exception) { return null; }
        }

        public static void Write(string markerPath, string buildVersion)
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(markerPath));
                File.WriteAllText(markerPath, (buildVersion ?? "") + "\n" +
                                  DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ", CultureInfo.InvariantCulture) + "\n");
            }
            catch (Exception) { }  // never break the game over a marker
        }

        public static void Clear(string markerPath)
        {
            try { if (File.Exists(markerPath)) File.Delete(markerPath); }
            catch (Exception) { }
        }

        /// <summary>Unity keeps the previous run's log next to the current one: Player.log → Player-prev.log.</summary>
        public static string PreviousLogPath(string consoleLogPath)
        {
            if (string.IsNullOrEmpty(consoleLogPath)) return null;
            var dir = Path.GetDirectoryName(consoleLogPath) ?? "";
            return Path.Combine(dir, Path.GetFileNameWithoutExtension(consoleLogPath) + "-prev" + Path.GetExtension(consoleLogPath));
        }

        /// <summary>
        /// The last <paramref name="maxBytes"/> of the file, starting at a whole line; "" when it is
        /// missing. Usually the whole log fits; a log that spammed every frame keeps its end, where the crash is.
        /// </summary>
        public static string ReadTail(string path, int maxBytes = MaxLogBytes)
        {
            try
            {
                if (string.IsNullOrEmpty(path) || !File.Exists(path)) return "";
                using (var f = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                {
                    long start = Math.Max(0, f.Length - maxBytes);
                    f.Seek(start, SeekOrigin.Begin);
                    var bytes = new byte[f.Length - start];
                    int read = 0;
                    while (read < bytes.Length)
                    {
                        int n = f.Read(bytes, read, bytes.Length - read);
                        if (n <= 0) break;
                        read += n;
                    }
                    int from = 0;
                    if (start > 0)  // cut mid-line (maybe mid-character): skip to the next line
                    {
                        int nl = Array.IndexOf(bytes, (byte)'\n', 0, read);
                        from = nl >= 0 ? nl + 1 : 0;
                    }
                    return Encoding.UTF8.GetString(bytes, from, read - from);
                }
            }
            catch (Exception) { return ""; }
        }
    }
}
