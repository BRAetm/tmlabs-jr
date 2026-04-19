using System;
using System.IO;
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
    private const string PlayUrl = "https://www.xbox.com/play";

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
        Loaded += async (_, _) => await InitAsync();
        Unloaded += (_, _) => Cleanup();
    }

    private async Task InitAsync()
    {
        try
        {
            var dataDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "PSRemotePlay", "WebView2-xboxrp");
            Directory.CreateDirectory(dataDir);

            // Flip on Edge's Xbox Cloud Gaming 1080p / clarity-boost feature flags so
            // xcloud is willing to serve 1080p to this WebView.
            var envOpts = new CoreWebView2EnvironmentOptions(
                "--enable-features=msEdgeXboxCloudGaming1080p,msEdgeXboxCloudGamingClarityBoost " +
                "--disable-features=msEdgeXboxCloudGamingNoHDR");
            var env = await CoreWebView2Environment.CreateAsync(null, dataDir, envOpts);
            await Web.EnsureCoreWebView2Async(env);

            Web.CoreWebView2.Settings.UserAgent =
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " +
                "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 Edg/120.0.0.0";

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

            Web.CoreWebView2.NavigationCompleted += (_, _) =>
            {
                _sink?.InjectShim();
                // Gamepad input only reaches the page if the WebView owns focus.
                try { Web.Focus(); } catch { }
            };
            Web.CoreWebView2.Navigate(PlayUrl);
        }
        catch (Exception ex)
        {
            MessageBox.Show($"Failed to init: {ex.Message}");
        }
    }

    private void Cleanup()
    {
        if (_driver != null)
        {
            _ = _driver.DisposeAsync();
            _driver = null;
        }
    }
}
