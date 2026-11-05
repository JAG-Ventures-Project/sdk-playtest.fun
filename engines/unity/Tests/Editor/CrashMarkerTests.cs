// The previous-run crash detection: the marker file and reading the end of the previous log.

using System.IO;
using System.Text;
using NUnit.Framework;

namespace PlaytestFunSdk.Tests
{
    public class CrashMarkerTests
    {
        string dir;

        [SetUp]
        public void SetUp()
        {
            dir = Path.Combine(Path.GetTempPath(), "ptf-crash-" + System.Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(dir);
        }

        [TearDown]
        public void TearDown() => Directory.Delete(dir, true);

        [Test]
        public void NoMarkerMeansTheLastRunQuitNormally()
        {
            Assert.IsNull(CrashMarker.Read(Path.Combine(dir, "running")));
        }

        [Test]
        public void AMarkerLeftBehindIsAnAbnormalExit()
        {
            var marker = Path.Combine(dir, "sub", "running");
            CrashMarker.Write(marker, "f1");
            var previous = CrashMarker.Read(marker);
            Assert.IsNotNull(previous);
            Assert.AreEqual("f1", previous.BuildVersion);
            StringAssert.Contains("build f1", previous.Message);
            StringAssert.IsMatch(@"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$", previous.StartedUtc);
        }

        [Test]
        public void ClearingTheMarkerOnQuitMeansNoReport()
        {
            var marker = Path.Combine(dir, "running");
            CrashMarker.Write(marker, "f1");
            CrashMarker.Clear(marker);
            Assert.IsNull(CrashMarker.Read(marker));
            CrashMarker.Clear(marker);  // twice is fine
        }

        [Test]
        public void ThePreviousLogSitsNextToTheCurrentOne()
        {
            var path = CrashMarker.PreviousLogPath(Path.Combine("C:", "Logs", "Player.log"));
            Assert.AreEqual(Path.Combine("C:", "Logs", "Player-prev.log"), path);
            Assert.IsNull(CrashMarker.PreviousLogPath(""));
        }

        [Test]
        public void ASmallLogIsSentWhole()
        {
            var path = Path.Combine(dir, "Player-prev.log");
            File.WriteAllText(path, "Mono path\nInitialize engine\nCrash!!!\n");
            Assert.AreEqual("Mono path\nInitialize engine\nCrash!!!\n", CrashMarker.ReadTail(path));
        }

        [Test]
        public void ALargeLogKeepsItsEndFromAWholeLine()
        {
            var path = Path.Combine(dir, "Player-prev.log");
            var sb = new StringBuilder();
            for (int i = 0; i < 5000; i++) sb.Append("frame spam ").Append(i).Append(" ü\n");
            sb.Append("Crash!!!\n");
            File.WriteAllText(path, sb.ToString());

            var tail = CrashMarker.ReadTail(path, 1000);
            Assert.LessOrEqual(Encoding.UTF8.GetByteCount(tail), 1000);
            StringAssert.EndsWith("Crash!!!\n", tail);
            StringAssert.StartsWith("frame spam ", tail);
            Assert.AreEqual(-1, tail.IndexOf('\uFFFD'), "no character cut in half");  // ordinal: culture compare ignores U+FFFD
        }

        // The shape of a real Windows native crash log: last game lines, the crash, the DLL list, the stack.
        static string NativeCrashLog(int modules)
        {
            var sb = new StringBuilder();
            sb.Append("Driver:          32.0.15.7329\r\n");
            sb.Append("Game: entering the beach\r\n");
            sb.Append("Forcing a crash -- Intentionally Dereferencing NULL pointer\r\n");
            sb.Append("Crash!!!\r\n");
            sb.Append("SymInit: Symbol-SearchPath: '.;C:\\Users\\ali\\Game;C:\\WINDOWS;', symOptions: 534, UserName: 'ali'\r\n");
            sb.Append("OS-Version: 10.0.0\r\n");
            for (int i = 0; i < modules; i++)
                sb.Append("C:\\WINDOWS\\SYSTEM32\\mod").Append(i).Append(".dll:mod").Append(i)
                  .Append(".dll (00007FF880540000), size: 53248 (result: 0), SymType: '-exported-', PDB: 'C:\\WINDOWS\\SYSTEM32\\mod")
                  .Append(i).Append(".dll', fileVersion: 10.0.26100.7705\r\n");
            sb.Append("\r\n========== OUTPUTTING STACK TRACE ==================\r\n\r\n");
            sb.Append("  ERROR: SymGetSymFromAddr64, GetLastError: 'Attempt to access invalid address.' (Address: 00007FFFD7BBBA09)\r\n");
            sb.Append("0x00007FFFD7BBBA09 (UnityPlayer) (function-name not available)\r\n");
            sb.Append("0x000001A7F1C3F943 (Mono JIT Code) Player:Update ()\r\n");
            sb.Append("========== END OF STACKTRACE ===========\r\n");
            return sb.ToString();
        }

        [Test]
        public void TheDllListAndSymbolNoiseAreDropped()
        {
            var log = CrashMarker.StripNoise(NativeCrashLog(232));
            StringAssert.DoesNotContain("SymType", log);
            StringAssert.DoesNotContain("SymInit", log);
            StringAssert.DoesNotContain("SymGetSymFromAddr64", log);
            StringAssert.DoesNotContain("UserName", log);
            StringAssert.Contains("Game: entering the beach\r\nForcing a crash", log);
            StringAssert.Contains("Crash!!!\r\nOS-Version: 10.0.0\r\n", log);
            StringAssert.Contains("Driver:          32.0.15.7329", log);
            StringAssert.Contains("(UnityPlayer) (function-name not available)\r\n0x000001A7F1C3F943 (Mono JIT Code) Player:Update ()", log);
            Assert.Less(Encoding.UTF8.GetByteCount(log), 1000);
        }

        [Test]
        public void ANativeCrashLogNowFitsWhole()
        {
            var path = Path.Combine(dir, "Player-prev.log");
            File.WriteAllText(path, NativeCrashLog(232));
            Assert.Greater(new FileInfo(path).Length, CrashMarker.MaxLogBytes, "the raw log would have been cut");

            var log = CrashMarker.TailBytes(CrashMarker.StripNoise(CrashMarker.ReadTail(path, CrashMarker.ReadBytes)));
            StringAssert.StartsWith("Driver:", log);
            StringAssert.Contains("Crash!!!", log);
            StringAssert.EndsWith("END OF STACKTRACE ===========\r\n", log);
        }

        [Test]
        public void TailBytesCutsAtALine()
        {
            Assert.AreEqual("short", CrashMarker.TailBytes("short", 100));
            Assert.AreEqual("ccc\n", CrashMarker.TailBytes("aaa\nbbb\nccc\n", 6));
            var tail = CrashMarker.TailBytes(new string('ü', 50), 11);  // no newline: still whole characters
            Assert.AreEqual(-1, tail.IndexOf('\uFFFD'));
            Assert.LessOrEqual(Encoding.UTF8.GetByteCount(tail), 11);
        }

        [Test]
        public void AMissingLogIsEmpty()
        {
            Assert.AreEqual("", CrashMarker.ReadTail(Path.Combine(dir, "none.log")));
            Assert.AreEqual("", CrashMarker.ReadTail(null));
        }

        [Test]
        public void OffByDefault()
        {
            var s = UnityEngine.ScriptableObject.CreateInstance<PlaytestFunSettings>();
            try { Assert.IsFalse(s.sendLogAfterCrash, "the log can hold hardware details and paths: opt in"); }
            finally { UnityEngine.Object.DestroyImmediate(s); }
        }
    }
}
