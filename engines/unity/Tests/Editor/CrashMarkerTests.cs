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
