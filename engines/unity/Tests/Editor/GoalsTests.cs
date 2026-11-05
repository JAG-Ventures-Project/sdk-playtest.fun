using System.Collections;
using NUnit.Framework;
using PlaytestFunSdk.Editor;
using UnityEngine.TestTools;

namespace PlaytestFunSdk.Tests
{
    public class GoalsTests
    {
        const string Reply = @"{""success"":true,""data"":[{""id"":""11111111-2222-4333-8444-555555555555"",
            ""title"":""Playtest f1"",""versionLabel"":""f1"",""publishState"":""published"",
            ""startsAt"":""2026-11-03T21:00:00.000Z"",""endsAt"":""2026-11-11T20:59:59.000Z"",
            ""goals"":[{""key"":""collect_points"",""title"":""Collect \""points\"""",""target"":120},
                       {""key"":""finish_tutorial"",""title"":""Finish the tutorial""},
                       {""slug"":""3rd-boss"",""name"":""Beat the 3rd boss""}]}]}";

        [Test]
        public void ParsesPlaytestsAndGoals()
        {
            var reply = PlaytestFunServer.Parse(Reply);
            Assert.IsTrue(reply.success);
            Assert.AreEqual(1, reply.data.Length);
            var pt = reply.data[0];
            Assert.AreEqual("Playtest f1", pt.title);
            Assert.AreEqual(3, pt.goals.Length);
            Assert.AreEqual("collect_points", pt.goals[0].Key);
            Assert.AreEqual(120, pt.goals[0].Target);
            Assert.AreEqual("3rd-boss", pt.goals[2].Key, "falls back to other key fields");
            Assert.AreEqual("Beat the 3rd boss", pt.goals[2].Title);
        }

        [Test]
        public void ParsesTheServerReplyShape()
        {
            // As GET /sdk/game/playtests answers today: "label" and a null target.
            var reply = PlaytestFunServer.Parse(@"{""success"":true,""data"":[{""id"":""d1"",""title"":""Playtest f1"",
                ""versionId"":""v1"",""versionLabel"":""f1"",""publishState"":""published"",
                ""goals"":[{""key"":""kill_1000_monster"",""label"":""Kill 1000 monster"",""target"":null}]}]}");
            var goal = reply.data[0].goals[0];
            Assert.AreEqual("kill_1000_monster", goal.Key);
            Assert.AreEqual("Kill 1000 monster", goal.Title);
            Assert.AreEqual(0, goal.Target);
            StringAssert.Contains("/// <summary>Kill 1000 monster</summary>", PlaytestFunCodegen.Generate(reply.data[0]));
        }

        [Test]
        public void ParsesTheCurrentEmptyReply()
        {
            var reply = PlaytestFunServer.Parse(@"{""success"":true,""data"":[{""id"":""x"",""title"":""t"",""goals"":[]}]}");
            Assert.AreEqual(0, reply.data[0].goals.Length);
            Assert.IsNull(PlaytestFunServer.Parse("not json"));
        }

        [TestCase("collect_points", "CollectPoints")]
        [TestCase("finish-tutorial", "FinishTutorial")]
        [TestCase("3rd boss", "_3rdBoss")]
        [TestCase("çık", "K")]
        [TestCase("", "_")]
        public void KeysBecomeIdentifiers(string key, string expected)
        {
            Assert.AreEqual(expected, PlaytestFunCodegen.Identifier(key));
        }

        [Test]
        public void GeneratesOneConstantPerGoal()
        {
            var code = PlaytestFunCodegen.Generate(PlaytestFunServer.Parse(Reply).data[0]);
            StringAssert.Contains("public static class PlaytestFunGoals", code);
            StringAssert.Contains("public const string PlaytestId = \"11111111-2222-4333-8444-555555555555\";", code);
            StringAssert.Contains("public const string CollectPoints = \"collect_points\";", code);
            StringAssert.Contains("public const double CollectPointsTarget = 120;", code);
            StringAssert.Contains("public const string FinishTutorial = \"finish_tutorial\";", code);
            StringAssert.DoesNotContain("FinishTutorialTarget", code);
            StringAssert.Contains("public const string _3rdBoss = \"3rd-boss\";", code);
            StringAssert.Contains("Collect &quot;points&quot; (target 120)", code);
        }

        [UnityTest, Explicit("Calls the real playtest.fun API with the project's game key")]
        public IEnumerator FetchesWithProjectGameKey()
        {
            var settings = PlaytestFunSettings.Load();
            Assume.That(settings != null && settings.gameKey.StartsWith("phub_gk_"), "needs a game key in Project Settings");
            bool done = false;
            bool ok = false;
            PlaytestFunServer.Fetch(settings.gameKey, r => { ok = r; done = true; });
            var deadline = UnityEditor.EditorApplication.timeSinceStartup + 20;
            while (!done && UnityEditor.EditorApplication.timeSinceStartup < deadline) yield return null;

            Assert.IsTrue(done, "no reply");
            Assert.AreEqual("", PlaytestFunServer.LastError);
            Assert.IsTrue(ok);
            var reply = PlaytestFunServer.Cached(settings.gameKey);
            Assert.IsNotNull(reply?.data);
            foreach (var pt in reply.data)
                UnityEngine.Debug.Log($"playtest.fun fetch: {pt.title} ({pt.id}) goals={pt.goals?.Length ?? 0}: " +
                                      string.Join(", ", System.Array.ConvertAll(pt.goals ?? new Goal[0], g => g.Key)));
        }

        [Test]
        public void DuplicateIdentifiersGetASuffix()
        {
            var pt = PlaytestFunServer.Parse(@"{""data"":[{""id"":""x"",""goals"":[{""key"":""a_b""},{""key"":""a-b""}]}]}").data[0];
            var code = PlaytestFunCodegen.Generate(pt);
            StringAssert.Contains("AB = \"a_b\"", code);
            StringAssert.Contains("AB2 = \"a-b\"", code);
        }

        static Playtest Pt(string id, params string[] keys) => new Playtest
        {
            id = id, title = id,
            goals = System.Array.ConvertAll(keys, k => new Goal { key = k }),
        };

        [Test]
        public void GeneratesIntoANamespace()
        {
            var code = PlaytestFunCodegen.Generate(Pt("x", "a"), "MyGame.Analytics", "Goals");
            StringAssert.StartsWith(PlaytestFunCodegen.Marker, code);
            StringAssert.Contains("namespace MyGame.Analytics\n{\n    /// <summary>", code);
            StringAssert.Contains("    public static class Goals\n", code);
            StringAssert.Contains("        public const string A = \"a\";", code);
            StringAssert.EndsWith("    }\n}\n", code);
        }

        [Test]
        public void GeneratesIntoPlaytestFunSdkByDefault()
        {
            var code = PlaytestFunCodegen.Generate(Pt("x", "a"));
            StringAssert.Contains("namespace PlaytestFunSdk\n{\n", code);
            StringAssert.Contains("    public static class PlaytestFunGoals\n", code);
        }

        [Test]
        public void EmptyNamespaceMeansGlobal()
        {
            var code = PlaytestFunCodegen.Generate(Pt("x", "a"), "");
            StringAssert.DoesNotContain("namespace", code);
            StringAssert.Contains("\npublic static class PlaytestFunGoals\n", code);
        }

        [Test]
        public void GoalNamedLikeTheClassGetsASuffix()
        {
            StringAssert.Contains("Goals2 = \"goals\"", PlaytestFunCodegen.Generate(Pt("x", "goals"), "", "Goals"));
        }

        [Test]
        public void ReadsThePlaytestIdBack()
        {
            Assert.AreEqual("a\"b", PlaytestFunCodegen.ReadPlaytestId(PlaytestFunCodegen.Generate(Pt("a\"b"))));
            Assert.IsNull(PlaytestFunCodegen.ReadPlaytestId("class X {}"));
        }

        [Test]
        public void StalenessIsNullWhenCurrent()
        {
            var pts = new[] { Pt("p1", "a", "b") };
            var code = PlaytestFunCodegen.Generate(pts[0]).Replace("\n", "\r\n");  // line endings do not count
            Assert.IsNull(PlaytestFunCodegen.Staleness(code, pts, "p1", PlaytestFunEditorSettings.DefaultNamespace, "PlaytestFunGoals", out var again));
            Assert.IsNull(again);
            Assert.IsNull(PlaytestFunCodegen.Staleness(code, null, "", PlaytestFunEditorSettings.DefaultNamespace, "PlaytestFunGoals", out _), "nothing fetched");
            Assert.IsNull(PlaytestFunCodegen.Staleness("// mine", pts, "p1", PlaytestFunEditorSettings.DefaultNamespace, "PlaytestFunGoals", out _), "not generated");
        }

        [Test]
        public void StalenessFindsChangedGoals()
        {
            var code = PlaytestFunCodegen.Generate(Pt("p1", "a"));
            var now = new[] { Pt("p1", "a", "b") };
            Assert.IsNotNull(PlaytestFunCodegen.Staleness(code, now, "", PlaytestFunEditorSettings.DefaultNamespace, "PlaytestFunGoals", out var again));
            Assert.AreSame(now[0], again);
            Assert.IsNotNull(PlaytestFunCodegen.Staleness(code, new[] { Pt("p1", "a") }, "", "Ns", "PlaytestFunGoals", out _),
                             "namespace option changed");
        }

        [Test]
        public void StalenessFindsAnotherPlaytest()
        {
            var code = PlaytestFunCodegen.Generate(Pt("p1", "a"));
            var pts = new[] { Pt("p1", "a"), Pt("p2", "c") };
            Assert.IsNotNull(PlaytestFunCodegen.Staleness(code, pts, "p2", PlaytestFunEditorSettings.DefaultNamespace, "PlaytestFunGoals", out var again));
            Assert.AreSame(pts[1], again);
            Assert.IsNotNull(PlaytestFunCodegen.Staleness(code, new[] { Pt("p3") }, "", PlaytestFunEditorSettings.DefaultNamespace, "PlaytestFunGoals", out again), "deleted");
            Assert.IsNull(again);
        }

        [Test]
        public void PicksThePlaytestIdOrTheOnlyPlaytest()
        {
            var one = new[] { Pt("p1") };
            var two = new[] { Pt("p1"), Pt("p2") };
            Assert.AreSame(one[0], PlaytestFunCodegen.Pick(one, "", out var error));
            Assert.IsNull(error);
            Assert.AreSame(two[1], PlaytestFunCodegen.Pick(two, "p2", out error));
            Assert.IsNull(PlaytestFunCodegen.Pick(two, "", out error));
            StringAssert.Contains("several", error);
            Assert.IsNull(PlaytestFunCodegen.Pick(two, "p9", out error));
            StringAssert.Contains("p9", error);
            Assert.IsNull(PlaytestFunCodegen.Pick(new Playtest[0], "", out error));
        }

        [TestCase("Assets/PlaytestFun/Goals.cs", true)]
        [TestCase("Assets/Goals.txt", false)]
        [TestCase("Packages/x/Goals.cs", false)]
        [TestCase("Assets/../Goals.cs", false)]
        [TestCase("", false)]
        public void ValidatesTheOutputPath(string path, bool ok) =>
            Assert.AreEqual(ok, PlaytestFunEditorSettings.ValidatePath(path) == null);

        [TestCase("", true)]
        [TestCase("MyGame.Analytics", true)]
        [TestCase("My Game", false)]
        [TestCase("MyGame.", false)]
        [TestCase("1Game", false)]
        public void ValidatesTheNamespace(string ns, bool ok) =>
            Assert.AreEqual(ok, PlaytestFunEditorSettings.ValidateNamespace(ns) == null);
    }
}
