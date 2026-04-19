using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using Labs.Engine.Core;
using Labs.Engine.Scripting;
using Microsoft.Web.WebView2.Core;

namespace Labs.RemotePlay.Panels;

public partial class XboxRemotePlayView : UserControl
{
    private const int SessionId = 3;
    private const string PlayUrl       = "https://www.xbox.com/play";
    private const string SignInUrl     = "https://login.live.com/login.srf?wa=wsignin1.0&rpsnv=13&wreply=https%3A%2F%2Fwww.xbox.com%2Fplay";
    private const string RemotePlayUrl = "https://www.xbox.com/play?launch=remoteplay";

    private bool _perfMode = false;

    private readonly FramePublisher _pub;
    private WebView2GamepadSink? _sink;
    private ScriptDriver? _driver;

    public ScriptDriver? Driver => _driver;
    public event Action? SessionEnded;

    public void SetMuted(bool muted)
    {
        try { if (Web?.CoreWebView2 != null) Web.CoreWebView2.IsMuted = muted; }
        catch { }
    }

    /// <summary>Forward a physical XInput state into the WebView2 gamepad shim.
    /// Keeps input flowing when the WPF window loses focus (browser Gamepad API stops).</summary>
    public void ForwardPhysicalInput(GamepadState state)
    {
        try { _sink?.SendInput(SessionId, state); } catch { }
    }

    public XboxRemotePlayView(FramePublisher pub)
    {
        _pub = pub;
        InitializeComponent();
        // Don't auto-init — wait for quality pick
        Unloaded += (_, _) => Cleanup();
    }

    // 1ms Windows timer resolution — reduces scheduling jitter from default 15.6ms.
    // Stays active for the lifetime of this view, released in Cleanup().
    [DllImport("winmm.dll")] private static extern int timeBeginPeriod(uint p);
    [DllImport("winmm.dll")] private static extern int timeEndPeriod(uint p);
    private bool _timerPeriodSet;

    private void PickHigh_Click(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        _perfMode = false;
        QualityPanel.Visibility = Visibility.Collapsed;
        _ = InitAsync();
    }

    private void PickPerf_Click(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        _perfMode = true;
        QualityPanel.Visibility = Visibility.Collapsed;
        _ = InitAsync();
    }

    private async Task InitAsync()
    {
        try
        {
            var dataDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "PSRemotePlay", "WebView2-xboxrp");
            Directory.CreateDirectory(dataDir);

            // 1ms timer resolution — tightens the browser's internal scheduling loop so
            // frames are decoded and presented with minimal jitter, even on bad WiFi.
            if (timeBeginPeriod(1) == 0) _timerPeriodSet = true;

            string features = _perfMode
                // Performance: no 1080p flag → xCloud serves 720p; no clarity boost (GPU sharpening);
                // keep D3D11VideoDecoder so hardware still decodes H.264 (saves CPU on low-end)
                ? "D3D11VideoDecoder"
                // High quality: 1080p, clarity boost, HEVC support, video overlay path
                : "msEdgeXboxCloudGaming1080p," +
                  "msEdgeXboxCloudGamingClarityBoost," +
                  "D3D11VideoDecoder," +
                  "UseMultiPlaneOverlayForVideoOnWindows," +
                  "PlatformHEVCDecoderSupport";

            var envOpts = new CoreWebView2EnvironmentOptions(
                $"--enable-features={features}," +
                    // mDNS obfuscation adds ~200ms to ICE gathering by doing a local DNS
                    // lookup for every host candidate. Disabling exposes the real LAN IP
                    // directly, which cuts connection setup time significantly.
                    "WebRtcHideLocalIpsWithMdns<disabled " +
                "--disable-features=msEdgeXboxCloudGamingNoHDR,WebRtcHideLocalIpsWithMdns " +
                // FlexFEC: Forward Error Correction for video — the browser sends redundant
                // packets so the server can reconstruct dropped frames without a retransmit
                // round-trip. Huge on bad WiFi where 2-5% packet loss is common.
                "--force-fieldtrials=" +
                    "WebRTC-FlexFEC-03-Advertised/Enabled/" +
                    "WebRTC-FlexFEC-03/Enabled/ " +
                "--disable-background-timer-throttling " +
                "--disable-renderer-backgrounding " +
                "--disable-backgrounding-occluded-windows " +
                "--enable-zero-copy " +
                "--enable-gpu-rasterization " +
                "--use-angle=d3d11 " +
                "--autoplay-policy=no-user-gesture-required " +
                "--enable-main-frame-before-activation " +
                // Don't let WebRTC throttle itself when CPU spikes
                "--webrtc-max-cpu-consumption-percentage=100 " +
                // Prioritize latency over smoothness in the WebRTC scheduler
                "--disable-rtc-smoothness-algorithm");
            var env = await CoreWebView2Environment.CreateAsync(null, dataDir, envOpts);
            await Web.EnsureCoreWebView2Async(env);

            // Performance mode: standard Chrome UA → xCloud serves 720p H.264
            // High quality: Edge 131 UA → unlocks 1080p + AV1 codec path
            Web.CoreWebView2.Settings.UserAgent = _perfMode
                ? "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
                : "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36 Edg/131.0.0.0";

            // Trim UI chrome that wastes cycles
            Web.CoreWebView2.Settings.IsStatusBarEnabled               = false;
            Web.CoreWebView2.Settings.AreDefaultContextMenusEnabled    = false;
            Web.CoreWebView2.Settings.IsSwipeNavigationEnabled         = false;

            // Block popups — allow login popups through, navigate http in same view, drop protocol URLs
            Web.CoreWebView2.NewWindowRequested += (_, args) =>
            {
                var uri = args.Uri ?? "";
                if (uri.Contains("login.live", StringComparison.OrdinalIgnoreCase) ||
                    uri.Contains("login.microsoftonline", StringComparison.OrdinalIgnoreCase) ||
                    uri.Contains("microsoftonline.com", StringComparison.OrdinalIgnoreCase) ||
                    uri.Contains("account.live", StringComparison.OrdinalIgnoreCase))
                    return;

                args.Handled = true;
                if (uri.StartsWith("http", StringComparison.OrdinalIgnoreCase))
                    Web.CoreWebView2.Navigate(uri);
            };

            // Block external app launches
            Web.CoreWebView2.NavigationStarting += (_, args) =>
            {
                var uri = args.Uri ?? "";
                if (!uri.StartsWith("http", StringComparison.OrdinalIgnoreCase) &&
                    !uri.StartsWith("data:", StringComparison.OrdinalIgnoreCase) &&
                    !uri.StartsWith("blob:", StringComparison.OrdinalIgnoreCase) &&
                    !uri.StartsWith("about:", StringComparison.OrdinalIgnoreCase))
                {
                    args.Cancel = true;
                }
            };

            try { Web.CoreWebView2.LaunchingExternalUriScheme += (_, args) => { args.Cancel = true; }; }
            catch { }

            _sink = new WebView2GamepadSink(SessionId);
            _sink.Attach(Web.CoreWebView2);
            _driver = new ScriptDriver(SessionId, _sink);

            // 1. Visibility spoof — xCloud reduces bitrate when document.hidden=true.
            //    Keep it always visible so the stream stays at full quality even when
            //    another WPF window is in front or the user alt-tabs.
            // 2. WebRTC SDP bitrate patch — xCloud's client caps b=AS to ~8000 kbps by
            //    default. Intercept setLocalDescription and remove the cap so the codec
            //    negotiation falls back to the server's own limit (up to ~20 Mbps).
            // 3. rAF FPS tick for the Labs Engine FPS pill.
            string perfJs = _perfMode ? "true" : "false";
            await Web.CoreWebView2.AddScriptToExecuteOnDocumentCreatedAsync($$"""
                (function () {
                    const PERF_MODE = {{perfJs}};

                    // ── 1. Visibility spoof ───────────────────────────────────────────────
                    // xCloud cuts bitrate when document.hidden=true. Lock it to visible.
                    try {
                        Object.defineProperty(document, 'visibilityState', { get: () => 'visible' });
                        Object.defineProperty(document, 'hidden',          { get: () => false });
                        const _ae = document.addEventListener.bind(document);
                        document.addEventListener = function (type, fn, opts) {
                            if (type === 'visibilitychange') return;
                            return _ae(type, fn, opts);
                        };
                    } catch (_) {}

                    // ── 2. RTCPeerConnection constructor intercept ────────────────────────
                    // Inject optimized ICE config before xCloud creates its peer connection.
                    try {
                        const _OrigPC = window.RTCPeerConnection;
                        window.RTCPeerConnection = function (cfg, con) {
                            if (cfg) {
                                cfg.iceCandidatePoolSize = 10; // pre-gather candidates before play
                                cfg.bundlePolicy  = 'max-bundle'; // one DTLS handshake for all tracks
                                cfg.rtcpMuxPolicy = 'require';    // halves UDP packet overhead
                                cfg.iceTransportPolicy = 'all';   // always try direct path first
                            }
                            const pc = new _OrigPC(cfg, con);

                            // Mark outgoing tracks as high network priority → DSCP queue-jump
                            // on routers/WiFi APs that respect WMM/QoS
                            const _addTrack = pc.addTrack.bind(pc);
                            pc.addTrack = function (track, ...streams) {
                                const sender = _addTrack(track, ...streams);
                                try {
                                    const p = sender.getParameters();
                                    if (p.encodings?.length) {
                                        p.encodings.forEach(e => {
                                            e.networkPriority = 'high';
                                            e.priority        = 'high';
                                        });
                                        sender.setParameters(p).catch(() => {});
                                    }
                                } catch (_) {}
                                return sender;
                            };

                            return pc;
                        };
                        // Preserve prototype chain — bound functions have no .prototype so
                        // we must copy from the original class, not the bound version
                        window.RTCPeerConnection.prototype = _OrigPC.prototype;
                        window.RTCPeerConnection.generateCertificate =
                            _OrigPC.generateCertificate?.bind(_OrigPC);
                    } catch (_) {}

                    // ── 3. SDP patch ──────────────────────────────────────────────────────
                    try {
                        const _sld = RTCPeerConnection.prototype.setLocalDescription;
                        RTCPeerConnection.prototype.setLocalDescription = function (desc, ...rest) {
                            if (desc?.sdp) {
                                let sdp = desc.sdp;
                                if (PERF_MODE) {
                                    sdp = sdp.replace(/\r?\nb=AS:\d+/g,   '\r\nb=AS:6000')
                                             .replace(/\r?\nb=TIAS:\d+/g, '\r\nb=TIAS:6000000');
                                } else {
                                    // Remove bitrate cap — server negotiates its own max (~20 Mbps)
                                    sdp = sdp.replace(/\r?\nb=AS:\d+/g,   '')
                                             .replace(/\r?\nb=TIAS:\d+/g, '');
                                }
                                // Reduce RTCP packet size (less overhead on every frame)
                                if (!sdp.includes('a=rtcp-rsize')) sdp += '\r\na=rtcp-rsize';
                                desc = new RTCSessionDescription({ type: desc.type, sdp });
                            }
                            return _sld.call(this, desc, ...rest);
                        };
                    } catch (_) {}

                    // ── 4. Fetch/XHR priority boost ───────────────────────────────────────
                    // xCloud's signalling and session API calls hit faster when queued high.
                    try {
                        const _fetch = window.fetch;
                        window.fetch = function (input, init = {}) {
                            init.priority = init.priority ?? 'high';
                            return _fetch.call(this, input, init);
                        };
                    } catch (_) {}

                    // ── 5. FPS tick ───────────────────────────────────────────────────────
                    (function tick() {
                        if (window.chrome?.webview) window.chrome.webview.postMessage('f');
                        requestAnimationFrame(tick);
                    })();
                })();
                """);
            Web.CoreWebView2.WebMessageReceived += (_, _) => _pub.NotifyFrame(SessionId);

            Web.CoreWebView2.NavigationCompleted += (_, e) =>
            {
                _sink?.InjectShim();
                try { Web.Focus(); } catch { }
                // Update URL bar
                Dispatcher.BeginInvoke(() => UrlBar.Text = Web.Source?.ToString() ?? "");
            };
            Web.CoreWebView2.SourceChanged += (_, _) =>
                Dispatcher.BeginInvoke(() => UrlBar.Text = Web.Source?.ToString() ?? "");
            Web.CoreWebView2.Navigate(PlayUrl);
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Failed to init: {ex.Message}");
        }
    }

    private void Back_Click(object sender, RoutedEventArgs e)
    {
        try { if (Web.CoreWebView2?.CanGoBack == true) Web.CoreWebView2.GoBack(); } catch { }
    }

    private void Refresh_Click(object sender, RoutedEventArgs e)
    {
        try { Web.CoreWebView2?.Reload(); } catch { }
    }

    private void GoSignIn_Click(object sender, RoutedEventArgs e)
    {
        try { Web.CoreWebView2?.Navigate(SignInUrl); } catch { }
    }

    private void GoRemotePlay_Click(object sender, RoutedEventArgs e)
    {
        try { Web.CoreWebView2?.Navigate(RemotePlayUrl); } catch { }
    }


    /// <summary>
    /// Fully tears down the Xbox session:
    /// 1. Clears cookies + session storage so Microsoft's servers mark the session ended.
    /// 2. Navigates to about:blank to drop the WebRTC connection.
    /// 3. Disposes the WebView2 control.
    /// 4. Force-kills any lingering msedgewebview2.exe processes so nothing holds the
    ///    auth token open in the background (which blocks Xbox PC app Remote Play).
    /// </summary>
    public async Task TerminateSessionAsync()
    {
        try
        {
            if (Web?.CoreWebView2?.Profile != null)
            {
                // Wipe cookies + session storage — this is what actually tells Microsoft's
                // servers the session is over. Without this the token stays valid for hours.
                await Web.CoreWebView2.Profile.ClearBrowsingDataAsync(
                    CoreWebView2BrowsingDataKinds.Cookies |
                    CoreWebView2BrowsingDataKinds.IndexedDb |
                    CoreWebView2BrowsingDataKinds.AllDomStorage);
            }
            Web.CoreWebView2?.Navigate("about:blank");
        }
        catch { }

        if (_timerPeriodSet) { timeEndPeriod(1); _timerPeriodSet = false; }
        if (_driver != null) { try { await _driver.DisposeAsync(); } catch { } _driver = null; }
        try { Web.Dispose(); } catch { }

        // Kill any msedgewebview2.exe still holding the session in the background.
        // Brief delay so WebView2 can flush its shutdown, then terminate survivors.
        _ = Task.Run(async () =>
        {
            await Task.Delay(800);
            try
            {
                foreach (var p in System.Diagnostics.Process.GetProcessesByName("msedgewebview2"))
                    try { p.Kill(); } catch { }
            }
            catch { }
        });
    }

    public void ReleaseXboxSession()
    {
        try { Web.CoreWebView2?.Navigate("about:blank"); } catch { }
    }

    private void Cleanup() => _ = TerminateSessionAsync();

    private void FreeSlot_Click(object sender, RoutedEventArgs e)
    {
        _ = TerminateSessionAsync();
        UrlBar.Text = "session terminated — Xbox PC app can now connect";
    }
}
