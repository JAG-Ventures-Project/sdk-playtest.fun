// Play mode tests of starting the SDK from settings. No SteamID is set and no events are
// queued, so nothing reaches the network or stays on disk.

using System.Collections;
using System.Text.RegularExpressions;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;

namespace PlaytestFunSdk.Tests
{
    public class BootstrapTests
    {
        PlaytestFunSettings settings;
        bool projectSdkWasRunning;

        [UnitySetUp]
        public IEnumerator SetUp()
        {
            // The project's own settings may have started the SDK already: stop it, restart it after.
            projectSdkWasRunning = PlaytestFun.Instance != null;
            if (projectSdkWasRunning) Object.Destroy(PlaytestFun.Instance.gameObject);
            yield return null;
            settings = ScriptableObject.CreateInstance<PlaytestFunSettings>();
            settings.gameKey = "phub_gk_test";
        }

        [UnityTearDown]
        public IEnumerator TearDown()
        {
            if (PlaytestFun.Instance != null) Object.Destroy(PlaytestFun.Instance.gameObject);
            Object.Destroy(settings);
            yield return null;
            if (projectSdkWasRunning) PlaytestFun.StartFromSettings();
        }

        [UnityTest]
        public IEnumerator StartsFromSettingsAndSurvivesSceneChanges()
        {
            var sdk = PlaytestFun.Bootstrap(settings);
            Assert.IsNotNull(sdk);
            Assert.AreSame(sdk, PlaytestFun.Instance);
            Assert.AreSame(sdk, PlaytestFun.Bootstrap(settings), "a second bootstrap reuses the running SDK");
            Assert.IsNotEmpty(sdk.GetStats().SessionId);
            Assert.AreEqual("DontDestroyOnLoad", sdk.gameObject.scene.name);
            yield return null;
        }

        [UnityTest]
        public IEnumerator SendsNothingInTheEditorWhenRunInEditorIsOff()
        {
            settings.runInEditor = false;
            var sdk = PlaytestFun.Bootstrap(settings);
            Assert.IsNotNull(sdk, "the instance exists so game code needs no Editor checks");
            Assert.AreSame(sdk, PlaytestFun.Instance);
            Assert.IsFalse(sdk.IsRunning);
            Assert.IsFalse(sdk.SetSteamId(76561198000000000ul));
            Assert.IsFalse(sdk.Goal("finish_tutorial"));
            Assert.IsEmpty(sdk.GetStats().SessionId, "no native session was started");
            yield return null;
        }

        [UnityTest]
        public IEnumerator DoesNotStartWithoutGameKey()
        {
            settings.gameKey = "";
            LogAssert.Expect(LogType.Warning, new Regex("set the game key"));
            Assert.IsNull(PlaytestFun.Bootstrap(settings));
            Assert.IsNull(PlaytestFun.Instance);
            yield return null;
        }

        [UnityTest]
        public IEnumerator SceneComponentYieldsToRunningSdk()
        {
            var sdk = PlaytestFun.Bootstrap(settings);

            var dedicated = new GameObject("dedicated");
            dedicated.AddComponent<PlaytestFun>();
            var shared = new GameObject("shared", typeof(BoxCollider));
            shared.AddComponent<PlaytestFun>();
            LogAssert.Expect(LogType.Warning, new Regex("already running"));
            LogAssert.Expect(LogType.Warning, new Regex("already running"));
            yield return null;

            Assert.IsTrue(dedicated == null, "a dedicated object goes away");
            Assert.IsTrue(shared != null && shared.GetComponent<PlaytestFun>() == null,
                          "only the component goes when the object holds other components");
            Assert.AreSame(sdk, PlaytestFun.Instance);
            Object.Destroy(shared);
        }
    }
}
