using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;
using Labs.Engine.Core;
using Labs.Engine.Scripting;
using Windows.Graphics.Capture;
using WinRT.Interop;

namespace Labs.RemotePlay.Panels;

public partial class XboxRpView : UserControl
{
    private readonly ObservableCollection<AppWindowInfo> _windows = new();
    private AppWindowInfo? _selected;
    private WindowCaptureSource? _source;
    private WindowCapture? _pickerWgc;   // set when session was started via OS picker
    private CaptureCardSink? _sink;
    private ScriptDriver? _driver;
    private readonly FramePublisher _pub;
    private int _pendingBlit;
    private int _frameCount;
    private DateTime _streamStart;

    public ScriptDriver? Driver => _driver;
    public event Action? SessionEnded;

    public XboxRpView(FramePublisher pub)
    {
        _pub = pub;
        InitializeComponent();
        WindowList.ItemsSource = _windows;
        Loaded += async (_, _) => await ScanAsync();
        Unloaded += async (_, _) => await StopCaptureAsync();
    }

    private async Task ScanAsync()
    {
        StatusBox.Text = "scanning for Xbox app…";
        _windows.Clear();
        var list = await Task.Run(() => WindowCaptureSource.FindXboxRemotePlay());
        foreach (var w in list)
            _windows.Add(w);

        if (_windows.Count == 0)
        {
            EmptyHint.Visibility = Visibility.Visible;
            StatusBox.Text = "no Xbox windows found";
        }
        else
        {
            EmptyHint.Visibility = Visibility.Collapsed;
            StatusBox.Text = $"{_windows.Count} window(s) found — select one";

            // Auto-start only when exactly one Xbox-specific window is found and it's
            // large enough to be a real stream (≥1280×720). Skip auto-start for fallback lists.
            if (_windows.Count == 1)
            {
                var w = _windows[0];
                var proc  = w.ProcessName.ToLowerInvariant();
                var title = w.Title.ToLowerInvariant();
                bool isXbox = proc is "xboxpcapp" or "xbox" or "gamebar" or "xboxapp" or "msxboxapp"
                              || proc == "applicationframehost"
                              || title.Contains("xbox") || title.Contains("remote play");
                if (isXbox && w.Width >= 1280 && w.Height >= 720)
                {
                    StatusBox.Text = $"Xbox window found ({w.Width}×{w.Height}) — starting…";
                    WindowList.SelectedIndex = 0;
                    Start_Click(this, null!);
                }
            }
        }
    }

    private async void Rescan_Click(object sender, RoutedEventArgs e) => await ScanAsync();

    /// <summary>Opens the OS system window/monitor picker. Works for any window including
    /// DRM-protected Xbox streams. The returned GraphicsCaptureItem goes straight into
    /// WindowCapture.StartWithItem — no HWND needed.</summary>
    private async void PickWindow_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var picker = new GraphicsCapturePicker();
            // Parent the picker to our WPF window
            var hwnd = new System.Windows.Interop.WindowInteropHelper(
                Window.GetWindow(this)).Handle;
            InitializeWithWindow.Initialize(picker, hwnd);

            var item = await picker.PickSingleItemAsync();
            if (item == null) return;

            await StartWithCaptureItem(item);
        }
        catch (Exception ex)
        {
            StatusBox.Text = "picker error: " + ex.Message;
        }
    }

    private async Task StartWithCaptureItem(GraphicsCaptureItem item)
    {
        try
        {
            const int sessionId = 3;
            // Build a minimal WindowCaptureSource-like wrapper using the item directly
            var wgc = new WindowCapture();
            wgc.FrameReady += bmp =>
            {
                if (System.Threading.Interlocked.Exchange(ref _pendingBlit, 1) == 1) return;
                Dispatcher.BeginInvoke(new Action(() =>
                {
                    try
                    {
                        VideoSurface.Source = bmp;
                        _pub.NotifyFrame(sessionId);
                        _frameCount++;
                        if (_frameCount % 60 == 0)
                        {
                            var fps = _frameCount / Math.Max(0.001, (DateTime.UtcNow - _streamStart).TotalSeconds);
                            StreamHud.Text = $"{item.DisplayName} · {bmp.PixelWidth}×{bmp.PixelHeight} · {fps:F0} fps · WGC";
                        }
                    }
                    catch { }
                    finally { System.Threading.Interlocked.Exchange(ref _pendingBlit, 0); }
                }));
            };
            wgc.StartWithItem(item);

            // Store as active capture (reuse _source slot via a thin wrapper isn't ideal,
            // so we stash the WindowCapture directly and clean it up in StopCaptureAsync)
            _pickerWgc = wgc;
            _sink   = new CaptureCardSink(sessionId);
            _driver = new ScriptDriver(sessionId, _sink);

            _frameCount  = 0;
            _streamStart = DateTime.UtcNow;
            PickerPanel.Visibility = Visibility.Collapsed;
            StreamPanel.Visibility = Visibility.Visible;
            StreamHud.Text = $"{item.DisplayName} · capturing";
        }
        catch (Exception ex)
        {
            StatusBox.Text = "start failed: " + ex.Message;
        }
    }

    private async void OpenXboxRemotePlay_Click(object sender, RoutedEventArgs e)
    {
        LaunchXboxToRemotePlay();
        StatusBox.Text = "xbox app launched — start remote play, then rescan";
        // Give the app a moment to bring up its window then rescan so the list populates
        await Task.Delay(2500);
        await ScanAsync();
    }

    private void WindowList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        _selected = WindowList.SelectedItem as AppWindowInfo;
        StartButton.IsEnabled = _selected != null;
    }

    private async void Start_Click(object sender, RoutedEventArgs e)
    {
        if (_selected == null) return;
        try
        {
            const int sessionId = 3;
            _source = new WindowCaptureSource(sessionId, _selected.Hwnd, _selected.Title);
            _source.TargetFps = 120;                  // bumped from 60 — WGC keeps up naturally
            _source.BitmapReady += OnBitmapReady;     // low-latency UI path (no JPEG round-trip)
            _source.FrameReady  += OnFrame;           // legacy JPEG path stays for ZMQ consumers
            await _source.StartAsync();

            _sink = new CaptureCardSink(sessionId);
            _driver = new ScriptDriver(sessionId, _sink);

            _frameCount = 0;
            _streamStart = DateTime.UtcNow;
            PickerPanel.Visibility = Visibility.Collapsed;
            StreamPanel.Visibility = Visibility.Visible;
            StreamHud.Text = $"{_selected.Title} · capturing";
        }
        catch (Exception ex)
        {
            StatusBox.Text = "start failed: " + ex.Message;
        }
    }

    private async void Stop_Click(object sender, RoutedEventArgs e)
    {
        await StopCaptureAsync();
        StreamPanel.Visibility = Visibility.Collapsed;
        PickerPanel.Visibility = Visibility.Visible;
        SessionEnded?.Invoke();
    }

    private async Task StopCaptureAsync()
    {
        // Remember which Xbox window we were attached to so we can disconnect it
        // (WGC stop alone leaves the Xbox app still streaming from the console).
        var capturedHwnd = _selected?.Hwnd ?? IntPtr.Zero;

        var d = _driver; _driver = null;
        if (d != null) try { await d.DisposeAsync(); } catch { }
        var k = _sink; _sink = null;
        if (k != null) try { k.Dispose(); } catch { }
        var pw = _pickerWgc; _pickerWgc = null;
        if (pw != null) try { pw.Dispose(); } catch { }
        var s = _source; _source = null;
        if (s != null)
        {
            try { s.BitmapReady -= OnBitmapReady; } catch { }
            try { s.FrameReady  -= OnFrame; }       catch { }
            try { await s.StopAsync(); } catch { }
            try { s.Dispose(); } catch { }
        }

        // Tell the Xbox app to end its session. WM_CLOSE is the reliable way — the Xbox
        // app has no stable public "end remote play" IPC, but closing the window ends
        // the stream cleanly and tears down the console link.
        if (capturedHwnd != IntPtr.Zero && IsWindow(capturedHwnd))
            PostMessage(capturedHwnd, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
    }

    // Best-effort launch of the Xbox app into its Remote Play section. Tries protocol URIs
    // first (if the app registers them), then falls back to the UWP AppsFolder launcher.
    private static void LaunchXboxToRemotePlay()
    {
        string[] deeplinks =
        {
            "xbox://remoteplay/",
            "msxbox-console-companion://remoteplay",
            "ms-xbox-gaming://remoteplay",
        };
        foreach (var uri in deeplinks)
        {
            try
            {
                Process.Start(new ProcessStartInfo(uri) { UseShellExecute = true });
                return;
            }
            catch { /* URI not registered — try next */ }
        }
        try
        {
            Process.Start(new ProcessStartInfo("explorer.exe",
                "shell:AppsFolder\\Microsoft.GamingApp_8wekyb3d8bbcc!App")
            { UseShellExecute = true });
        }
        catch { }
    }

    // --- Win32 interop for WM_CLOSE ---
    private const uint WM_CLOSE = 0x0010;

    [DllImport("user32.dll")]
    private static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool IsWindow(IntPtr hWnd);

    /// <summary>Direct-blit path — the frame is already a frozen BitmapSource, just assign it.
    /// No JPEG encode on sender, no decode on receiver. Throttled to one pending blit at a time
    /// so we never queue stale frames if the UI thread falls behind.</summary>
    private void OnBitmapReady(int sessionId, System.Windows.Media.Imaging.BitmapSource bmp)
    {
        if (System.Threading.Interlocked.Exchange(ref _pendingBlit, 1) == 1) return;
        Dispatcher.BeginInvoke(new Action(() =>
        {
            try
            {
                VideoSurface.Source = bmp;
                _pub.NotifyFrame(sessionId);
                _frameCount++;
                if (_frameCount % 60 == 0)
                {
                    var fps = _frameCount / Math.Max(0.001, (DateTime.UtcNow - _streamStart).TotalSeconds);
                    StreamHud.Text = $"{_source?.SourceName} · {bmp.PixelWidth}×{bmp.PixelHeight} · {fps:F0} fps · WGC direct";
                }
            }
            catch { }
            finally { System.Threading.Interlocked.Exchange(ref _pendingBlit, 0); }
        }));
    }

    private void OnFrame(int sessionId, byte[] jpeg)
    {
        try { _pub.PublishRawFrame(sessionId, jpeg); } catch { }
        if (System.Threading.Interlocked.Exchange(ref _pendingBlit, 1) == 1) return;
        var bytes = jpeg;
        Dispatcher.BeginInvoke(new Action(() =>
        {
            try
            {
                using var ms = new MemoryStream(bytes);
                var bmp = new BitmapImage();
                bmp.BeginInit();
                bmp.CacheOption = BitmapCacheOption.OnLoad;
                bmp.StreamSource = ms;
                bmp.EndInit();
                bmp.Freeze();
                VideoSurface.Source = bmp;
                _frameCount++;
                if (_frameCount % 30 == 0)
                {
                    var fps = _frameCount / Math.Max(0.001, (DateTime.UtcNow - _streamStart).TotalSeconds);
                    StreamHud.Text = $"{_source?.SourceName} · {bmp.PixelWidth}×{bmp.PixelHeight} · {fps:F0} fps";
                }
            }
            catch { }
            finally { System.Threading.Interlocked.Exchange(ref _pendingBlit, 0); }
        }));
    }
}
