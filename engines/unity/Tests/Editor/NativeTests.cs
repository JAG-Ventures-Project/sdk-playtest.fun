// Offline tests of the native plugin through the same P/Invoke layer the component uses.
// HTTP goes to a mock callback, so nothing reaches the network.

using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using NUnit.Framework;

namespace PlaytestFunSdk.Tests
{
    public class NativeTests
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        delegate int HttpFn(IntPtr user, IntPtr method, IntPtr url, IntPtr headers, IntPtr body,
                            IntPtr outBody, UIntPtr outCapacity, int timeoutMs);

        // Kept in a static field so the GC never collects it while native code holds it.
        static readonly HttpFn MockHttp = OnHttp;
        static int requests;

        [AOT.MonoPInvokeCallback(typeof(HttpFn))]
        static int OnHttp(IntPtr user, IntPtr method, IntPtr url, IntPtr headers, IntPtr body,
                          IntPtr outBody, UIntPtr outCapacity, int timeoutMs)
        {
            Interlocked.Increment(ref requests);
            var reply = Marshal.PtrToStringAnsi(method) == "GET"
                ? "{\"data\":{\"linked\":false,\"accepted\":false}}"
                : "{\"data\":{\"sessionPlaytimeSeconds\":1,\"totalPlaytimeSeconds\":2}}";
            var bytes = Encoding.UTF8.GetBytes(reply);
            int n = Math.Min(bytes.Length, (int)outCapacity.ToUInt64() - 1);
            Marshal.Copy(bytes, 0, outBody, n);
            Marshal.WriteByte(outBody, n, 0);
            return 200;
        }

        IntPtr key;

        [SetUp]
        public void SetUp()
        {
            requests = 0;
            key = PlaytestFun.Utf8("phub_gk_test");
        }

        [TearDown]
        public void TearDown()
        {
            PlaytestFun.Native.ptf_shutdown();
            Marshal.FreeHGlobal(key);
        }

        PlaytestFun.Native.Config MockConfig()
        {
            var c = new PlaytestFun.Native.Config();
            PlaytestFun.Native.ptf_config_init(ref c);
            c.game_key = key;
            c.open_url_on_shutdown = 0;
            c.http = Marshal.GetFunctionPointerForDelegate(MockHttp);
            return c;
        }

        static PlaytestFun.Native.StatsC WaitForTotal(double total)
        {
            PlaytestFun.Native.StatsC s = default;
            for (int i = 0; i < 300; ++i)
            {
                PlaytestFun.Native.ptf_get_stats(out s);
                if (s.server_total_seconds == total) break;
                Thread.Sleep(10);
            }
            return s;
        }

        [Test]
        public void NativeVersionMatchesPackage()
        {
            Assert.AreEqual("1.3.0", Marshal.PtrToStringAnsi(PlaytestFun.Native.ptf_version()));
        }

        [Test]
        public void ConfigLayoutMatchesNative()
        {
            var c = new PlaytestFun.Native.Config();
            PlaytestFun.Native.ptf_config_init(ref c);
            Assert.AreEqual(Marshal.SizeOf<PlaytestFun.Native.Config>(), (int)c.struct_size);
            Assert.AreEqual(1, c.open_url_on_shutdown);
        }

        [Test]
        public void InitRejectsMissingKey()
        {
            var c = new PlaytestFun.Native.Config();
            PlaytestFun.Native.ptf_config_init(ref c);
            Assert.AreEqual(0, PlaytestFun.Native.ptf_init(ref c));
        }

        [Test]
        public void SteamIdIsValidated()
        {
            var c = MockConfig();
            Assert.AreEqual(1, PlaytestFun.Native.ptf_init(ref c));
            Assert.AreEqual(0, PlaytestFun.Native.ptf_set_steam_id_str("not-a-steam-id"));
            Assert.AreEqual(1, PlaytestFun.Native.ptf_set_steam_id(76561198000000000ul));
        }

        [Test]
        public void SessionRoundTripsThroughMockHttp()
        {
            var c = MockConfig();
            Assert.AreEqual(1, PlaytestFun.Native.ptf_init(ref c));
            Assert.AreEqual(1, PlaytestFun.Native.ptf_set_steam_id_str("76561198000000000"));

            var s = WaitForTotal(2);
            Assert.AreEqual(2, s.server_total_seconds);
            Assert.AreEqual(200, s.last_http_status);
            Assert.Greater(Volatile.Read(ref requests), 0);
            Assert.IsNotEmpty(CString(s.session_id));
        }

        [Test]
        public void EventsAreQueuedAndBadJsonIsRejected()
        {
            var c = MockConfig();
            Assert.AreEqual(1, PlaytestFun.Native.ptf_init(ref c));
            Assert.AreEqual(1, PlaytestFun.Native.ptf_goal("finish_tutorial"));
            Assert.AreEqual(1, PlaytestFun.Native.ptf_goal_value("collect_points", 5));
            Assert.AreEqual(1, PlaytestFun.Native.ptf_test_result(12, 1, "Finished the tutorial ✓"));
            Assert.AreEqual(1, PlaytestFun.Native.ptf_event("GAME_STARTED", "OK", "{\"mode\":\"story\"}"));
            Assert.AreEqual(0, PlaytestFun.Native.ptf_event("GAME_STARTED", "OK", "not json"));
            Assert.AreEqual(0, PlaytestFun.Native.ptf_goal(""));
        }

        [Test]
        public void StatsAreUnavailableAfterShutdown()
        {
            var c = MockConfig();
            Assert.AreEqual(1, PlaytestFun.Native.ptf_init(ref c));
            PlaytestFun.Native.ptf_shutdown();
            Assert.AreEqual(0, PlaytestFun.Native.ptf_get_stats(out _));
        }

        [Test]
        public void ComponentStatsAreEmptyWhenNotRunning()
        {
            // In edit mode Awake never runs, so the component has no native session.
            var go = new UnityEngine.GameObject("PlaytestFun");
            try
            {
                var stats = go.AddComponent<PlaytestFun>().GetStats();
                Assert.AreEqual("", stats.SessionId);
                Assert.AreEqual("", stats.LastError);
                Assert.AreEqual(0, stats.LocalSessionSeconds);
            }
            finally
            {
                UnityEngine.Object.DestroyImmediate(go);
            }
        }

        static string CString(byte[] b)
        {
            int n = Array.IndexOf(b, (byte)0);
            return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n);
        }
    }
}
