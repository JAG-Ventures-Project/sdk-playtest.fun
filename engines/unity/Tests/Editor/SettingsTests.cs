using NUnit.Framework;
using UnityEngine;

namespace PlaytestFunSdk.Tests
{
    public class SettingsTests
    {
        [Test]
        public void DefaultsAreSafe()
        {
            var s = ScriptableObject.CreateInstance<PlaytestFunSettings>();
            try
            {
                Assert.IsTrue(s.startAutomatically);
                Assert.IsEmpty(s.gameKey, "no placeholder key, so an unconfigured project never calls the API");
                Assert.AreEqual(60u, s.heartbeatSeconds);
                Assert.IsTrue(s.openSiteOnQuit);
                Assert.IsFalse(s.openSiteInEditor);
                Assert.AreEqual("https://playtest.fun", s.exitUrl);
            }
            finally
            {
                Object.DestroyImmediate(s);
            }
        }
    }
}
