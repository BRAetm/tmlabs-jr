using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;
using Labs.Engine.Core;
using Labs.Engine.Scripting;

namespace Labs.RemotePlay.Panels;

public partial class XboxRpView : UserControl
{
    private readonly ObservableCollection<AppWindowInfo> _windows = new();
    private AppWindowInfo? _selected;
    private WindowCaptureSource? _source;
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
            StatusBox.Text = $"{_windows.Count} Xbox window(s) found";
        }
    }

    private async void Rescan_Click(object sender, RoutedEventArgs e) => await ScanAsync();

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
            _source.TargetFps = 60;
            _source.FrameReady += OnFrame;
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
        var d = _driver; _driver = null;
        if (d != null) try { await d.DisposeAsync(); } catch { }
        var k = _sink; _sink = null;
        if (k != null) try { k.Dispose(); } catch { }
        var s = _source; _source = null;
        if (s != null)
        {
            try { await s.StopAsync(); } catch { }
            try { s.Dispose(); } catch { }
        }
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
