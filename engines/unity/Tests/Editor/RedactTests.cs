// Personal data is removed from crash text; hardware and driver details stay.

using NUnit.Framework;

namespace PlaytestFunSdk.Tests
{
    public class RedactTests
    {
        static readonly string[] Names = Redact.Names("jdoe", "JDOE-LAPTOP", "Jo's iPhone");

        static string Scrub(string text, string home = @"C:\Users\jdoe") => Redact.Scrub(text, home, Names);

        [Test]
        public void ARealPlayerLogKeepsHardwareAndLosesPaths()
        {
            const string log =
                "Mono path[0] = 'C:/Users/jdoe/AppData/Local/Temp/game/ptf_Data/Managed'\n" +
                "[Subsystems] Discovering subsystems at path C:/Users/jdoe/AppData/Local/Temp/game/ptf_Data/UnitySubsystems\n" +
                "Initialize engine version: 6000.3.25f1 (e1dba0a9aba4)\n" +
                "[D3D12 Device Filter] Device Name: NVIDIA GeForce RTX 5060 Laptop GPU\n" +
                "[D3D12 Device Filter] Driver Version: 32.0.15.7329\n" +
                "    Renderer:        NVIDIA GeForce RTX 5060 Laptop GPU (ID=0x2d19)\n" +
                "    VRAM:            7810 MB\n" +
                "[Physics::Module] SDK Version: 4.1.2\n";
            var s = Scrub(log);
            StringAssert.DoesNotContain("jdoe", s.ToLowerInvariant());
            StringAssert.DoesNotContain("Users", s);
            StringAssert.DoesNotContain("Temp", s);
            StringAssert.Contains("Mono path[0] = '<path>/Managed'", s);
            StringAssert.Contains("subsystems at path <path>/UnitySubsystems\n", s);
            StringAssert.Contains("Driver Version: 32.0.15.7329", s);
            StringAssert.Contains("NVIDIA GeForce RTX 5060 Laptop GPU (ID=0x2d19)", s);
            StringAssert.Contains("VRAM:            7810 MB", s);
            StringAssert.Contains("6000.3.25f1 (e1dba0a9aba4)", s);
            StringAssert.Contains("[Physics::Module] SDK Version: 4.1.2", s);
        }

        [Test]
        public void PathsWithSpacesAndBackslashesKeepOnlyTheFileName()
        {
            Assert.AreEqual("Loading <path>/data.unity3d",
                            Scrub(@"Loading D:\Game Projects\My Game\ptf_Data\data.unity3d"));
            Assert.AreEqual("Failed to load '<path>/d3d11.dll'",
                            Scrub(@"Failed to load 'C:\Program Files\Foo\d3d11.dll'"));
            Assert.AreEqual("(at <path>/Player.cs:42)", Scrub("(at C:/Users/jdoe/Proj/Assets/Player.cs:42)"));
            Assert.AreEqual("share <path>/log.txt", Scrub(@"share \\NAS01\games\log.txt"));
        }

        [Test]
        public void AHomeWithASpaceIsRemovedWhole()
        {
            var s = Redact.Scrub(@"at C:\Users\John Smith\AppData\LocalLow\Co\Game\Player.log",
                                 @"C:\Users\John Smith", new string[0]);
            Assert.AreEqual("at <path>/Player.log", s);
        }

        [Test]
        public void MacLinuxAndAndroidPaths()
        {
            Assert.AreEqual("log at <path>/Player.log",
                            Redact.Scrub("log at /Users/jdoe/Library/Logs/Co/Game/Player.log", "/Users/jdoe", new string[0]));
            Assert.AreEqual("'<path>/Player.log'", Scrub("'/home/someone/.config/unity3d/Co/Game/Player.log'", ""));
            Assert.AreEqual("data <path>/files", Scrub("data /storage/emulated/0/Android/data/com.co.game/files", ""));
        }

        [Test]
        public void NamesSteamIdsAndEmailsAreReplaced()
        {
            Assert.AreEqual("Device: <name>, host <name>, user <name>",
                            Scrub("Device: Jo's iPhone, host JDOE-LAPTOP, user jdoe"));
            Assert.AreEqual("player <steamid> mail <email>", Scrub("player 76561198000000002 mail jo@example.com"));
            Assert.AreEqual("jdoena stays", Scrub("jdoena stays"), "only the whole name");
        }

        [Test]
        public void UrlsVersionsAndPlainTextStay()
        {
            const string text = "GET https://api.playtest.fun/v1/sdk/events 200\n" +
                                "Driver: 32.0.15.7329, 2026/11/05, ratio 3/4\n" +
                                "NullReferenceException: Object reference not set to an instance of an object\n" +
                                "Player.Update () (at <00000000000000000000000000000000>:0)\n";
            Assert.AreEqual(text, Scrub(text));
        }

        [Test]
        public void ShortNamesAreNotRemoved()
        {
            CollectionAssert.AreEqual(new[] { "jdoe" }, Redact.Names("", "  ", "pc", "jdoe", "JDOE"));
        }

        [Test]
        public void EmptyStaysEmpty()
        {
            Assert.AreEqual("", Scrub(null));
            Assert.AreEqual("", Scrub(""));
        }
    }
}
