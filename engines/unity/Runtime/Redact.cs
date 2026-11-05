// Strips personal data from crash text (Player log, exception messages and stacks) before it is
// queued: file paths keep only their file name, and the user, computer and device names and
// SteamIDs are replaced. Hardware and driver lines stay as they are.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace PlaytestFunSdk
{
    static class Redact
    {
        const string Home = "<home>";

        // C:\..., \\server\share\..., or <home>\...: segments may hold spaces ("Program Files"),
        // so a path runs to a quote, bracket, colon or line end.
        static readonly Regex WindowsPath = new Regex(
            @"(?:\b[A-Za-z]:|\\\\[^\\/\s'""]+|<home>)(?:[\\/][^\\/\r\n'""<>|*?()\[\]:]*)+",
            RegexOptions.Compiled | RegexOptions.CultureInvariant);

        // /Users/..., /home/..., /storage/...: at least one directory, no spaces.
        static readonly Regex UnixPath = new Regex(
            @"(?<=^|[\s'""(=\[,])/(?:[^/\s'""<>|*?()\[\]:]+/)+[^/\s'""<>|*?()\[\]:]*",
            RegexOptions.Compiled | RegexOptions.CultureInvariant | RegexOptions.Multiline);

        static readonly Regex SteamId = new Regex(@"\b7656119\d{10}\b", RegexOptions.Compiled);
        static readonly Regex Email = new Regex(@"\b[\w.+-]+@[\w-]+(?:\.[\w-]+)+\b", RegexOptions.Compiled);

        static string[] names = new string[0];
        static string home = "";

        /// <summary>Main thread, once: reads the names to remove (device name is main-thread only).</summary>
        public static void Init(string deviceName)
        {
            home = Safe(() => Environment.GetFolderPath(Environment.SpecialFolder.UserProfile));
            names = Names(Safe(() => Environment.UserName), Safe(() => Environment.MachineName), deviceName);
        }

        public static string Scrub(string text) => Scrub(text, home, names);

        internal static string Scrub(string text, string homeDir, IEnumerable<string> personalNames)
        {
            if (string.IsNullOrEmpty(text)) return text ?? "";
            if (!string.IsNullOrEmpty(homeDir) && homeDir.Length > 3)
                foreach (var form in new[] { homeDir, homeDir.Replace('\\', '/'), homeDir.Replace('/', '\\') }.Distinct())
                    text = Regex.Replace(text, Regex.Escape(form), Home, RegexOptions.IgnoreCase);
            text = WindowsPath.Replace(text, KeepFileName);
            text = UnixPath.Replace(text, KeepFileName);
            text = Email.Replace(text, "<email>");
            text = SteamId.Replace(text, "<steamid>");
            foreach (var name in personalNames ?? Enumerable.Empty<string>())
                text = Regex.Replace(text, @"(?<![\w])" + Regex.Escape(name) + @"(?![\w])", "<name>", RegexOptions.IgnoreCase);
            return text;
        }

        internal static string[] Names(params string[] candidates) =>
            candidates.Where(n => !string.IsNullOrWhiteSpace(n) && n.Trim().Length >= 3)
                      .Select(n => n.Trim())
                      .Distinct(StringComparer.OrdinalIgnoreCase)
                      .OrderByDescending(n => n.Length)  // "John's iPhone" before "John"
                      .ToArray();

        static string KeepFileName(Match m)
        {
            var path = m.Value.TrimEnd();
            int cut = Math.Max(path.LastIndexOf('/'), path.LastIndexOf('\\'));
            var file = cut >= 0 ? path.Substring(cut + 1) : "";
            return (file.Length > 0 ? "<path>/" + file : "<path>") + m.Value.Substring(path.Length);
        }

        static string Safe(Func<string> read)
        {
            try { return read() ?? ""; }
            catch (Exception) { return ""; }
        }
    }
}
