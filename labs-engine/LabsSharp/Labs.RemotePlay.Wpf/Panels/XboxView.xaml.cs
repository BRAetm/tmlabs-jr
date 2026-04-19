using System;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Labs.Engine.Core;
using Labs.Engine.Scripting;
using Microsoft.Web.WebView2.Core;

namespace Labs.RemotePlay.Panels;

public partial class XboxView : UserControl
{
    private readonly string _url;
    private readonly string _label;
    private readonly int _sessionId;
    private readonly FramePublisher _pub;
    private WebView2GamepadSink? _sink;
    private ScriptDriver? _driver;
    private DispatcherTimer? _captureTimer;
    private bool _capturing;

    public ScriptDriver? Driver => _driver;
    public event Action? SessionEnded;

    /// <param name="url">xCloud (xbox.com/play) or Xbox Remote Play URL.</param>
    /// <param name="label">Display label for the HUD.</param>
    public XboxView(string url, string label, int sessionId, FramePublisher pub)
    {
        InitializeComponent();
        _url = url;
        _label = label;
        _sessionId = sessionId;
        _pub = pub;
        Hud.Text = $"{label} · loading…";
        Loaded += async (_, _) => await InitAsync();
        Unloaded += async (_, _) =>
        {
            _captureTimer?.Stop();
            if (_driver != null) await _driver.DisposeAsync();
        };
    }

    private async System.Threading.Tasks.Task InitAsync()
    {
        try
        {
            var dataDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "PSRemotePlay", "WebView2-xbox");
            Directory.CreateDirectory(dataDir);
            var env = await CoreWebView2Environment.CreateAsync(null, dataDir);
            await Web.EnsureCoreWebView2Async(env);
            // Tell xbox.com we have a gamepad — gates xCloud's "play" button.
            Web.CoreWebView2.Settings.UserAgent =
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " +
                "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 Edg/120.0.0.0";
            // Block popups — only follow http links, drop store/xbox/protocol URLs
            Web.CoreWebView2.NewWindowRequested += (_, args) =>
            {
                args.Handled = true;
                var uri = args.Uri ?? "";
                if (uri.StartsWith("http", StringComparison.OrdinalIgnoreCase))
                    Web.CoreWebView2.Navigate(uri);
            };
            Web.CoreWebView2.NavigationCompleted += (_, nav) =>
            {
                Hud.Text = $"{_label} · {Web.CoreWebView2.DocumentTitle}";
                _sink?.InjectShim(); // re-install on every page nav

                // If this is Xbox Remote Play, auto-navigate to the console section
                if (_label == "Xbox Remote Play" && nav.IsSuccess)
                {
                    _ = Web.CoreWebView2.ExecuteScriptAsync(@"
                        (function tryConsole(attempts) {
                            if (attempts <= 0) return;
                            // Look for the 'Consoles' or 'Remote play' tab/link
                            var links = document.querySelectorAll('a, button, [role=""tab""], [role=""button""]');
                            for (var i = 0; i < links.length; i++) {
                                var txt = (links[i].textContent || '').toLowerCase().trim();
                                if (txt === 'consoles' || txt === 'console' || txt === 'remote play' || txt === 'my consoles') {
                                    links[i].click();
                                    return;
                                }
                            }
                            setTimeout(function(){ tryConsole(attempts - 1); }, 1000);
                        })(10);
                    ");
                }
            };

            // Wire the gamepad sink + script driver — scripts can now drive
            // the embedded xCloud / Xbox RP page via the JS shim.
            _sink = new WebView2GamepadSink(_sessionId);
            _sink.Attach(Web.CoreWebView2);
            _driver = new ScriptDriver(_sessionId, _sink);

            Web.CoreWebView2.Navigate(_url);

            _captureTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(66) };
            _captureTimer.Tick += async (_, _) => await CaptureFrameAsync();
            _captureTimer.Start();
        }
        catch (Exception ex) { Hud.Text = "WebView2 failed: " + ex.Message; }
    }

    private async System.Threading.Tasks.Task CaptureFrameAsync()
    {
        if (_capturing || Web.CoreWebView2 == null) return;
        _capturing = true;
        try
        {
            using var ms = new MemoryStream();
            await Web.CoreWebView2.CapturePreviewAsync(
                CoreWebView2CapturePreviewImageFormat.Jpeg, ms);
            if (ms.Length > 0)
                _pub.PublishRawFrame(_sessionId, ms.ToArray());
        }
        catch { }
        finally { _capturing = false; }
    }

    private void Stop_Click(object sender, RoutedEventArgs e)
    {
        _captureTimer?.Stop();
        SessionEnded?.Invoke();
    }
}
