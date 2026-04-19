using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;
using Labs.Engine.Core;
using Labs.Engine.Scripting;

namespace Labs.RemotePlay.Panels;

public partial class CaptureView : UserControl
{
    private readonly ObservableCollection<CaptureDeviceInfo> _devices = new();
    private CaptureDeviceInfo? _selected;
    private CaptureCardSource? _source;
    private CaptureCardSink? _sink;
    private ScriptDriver? _driver;
    private readonly FramePublisher _pub;
    private int _pendingBlit;
    private int _frameCount;
    private DateTime _streamStart;

    public ScriptDriver? Driver => _driver;
    public event Action? SessionEnded;

    public CaptureView(FramePublisher pub)
    {
        _pub = pub;
        InitializeComponent();
        DeviceList.ItemsSource = _devices;
        Loaded += async (_, _) => await ScanAsync();
        Unloaded += async (_, _) => await StopCaptureAsync();
    }

    private async Task ScanAsync()
    {
        StatusBox.Text = "scanning devices…";
        _devices.Clear();
        var list = await Task.Run(() => CaptureCardSource.ScanDevices(10));
        var seen = new HashSet<string>();
        foreach (var d in list)
        {
            // Dedupe — Windows enumerates some capture devices multiple times
            var key = d.Name.Split('(')[0].Trim();
            if (seen.Add(key)) _devices.Add(d);
        }
        StatusBox.Text = _devices.Count == 0
            ? "no capture devices found · plug one in and hit rescan"
            : $"{_devices.Count} device(s) found";
    }

    private async void Rescan_Click(object sender, RoutedEventArgs e) => await ScanAsync();

    private void DeviceList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        _selected = DeviceList.SelectedItem as CaptureDeviceInfo;
        StartButton.IsEnabled = _selected != null;
    }

    private async void Start_Click(object sender, RoutedEventArgs e)
    {
        if (_selected == null) return;
        try
        {
            const int sessionId = 1;

            // Resolve resolution: auto → device native (from scan probe), else parse combo tag.
            var (reqW, reqH) = ReadResolution();
            if (reqW == 0 || reqH == 0)
            {
                reqW = _selected.Width  > 0 ? _selected.Width  : 1920;
                reqH = _selected.Height > 0 ? _selected.Height : 1080;
            }
            int fps = ReadFps();

            _source = new CaptureCardSource(sessionId, _selected.Index, _selected.Name, reqW, reqH);
            _source.TargetFps = fps;
            _source.FrameReady += OnFrame;
            await _source.StartAsync();

            _sink = new CaptureCardSink(sessionId);
            _driver = new ScriptDriver(sessionId, _sink);

            _frameCount = 0;
            _streamStart = DateTime.UtcNow;
            PickerPanel.Visibility = Visibility.Collapsed;
            StreamPanel.Visibility = Visibility.Visible;
            StreamHud.Text = $"{_selected.Name} · {reqW}×{reqH} · {fps}fps · connecting…";
        }
        catch (Exception ex)
        {
            StatusBox.Text = "start failed: " + ex.Message;
        }
    }

    private (int w, int h) ReadResolution()
    {
        if (ResolutionCombo?.SelectedItem is ComboBoxItem item && item.Tag is string tag)
        {
            var parts = tag.Split('x');
            if (parts.Length == 2
                && int.TryParse(parts[0], out var w)
                && int.TryParse(parts[1], out var h))
                return (w, h);
        }
        return (0, 0);
    }

    private int ReadFps()
    {
        if (FpsCombo?.SelectedItem is ComboBoxItem item && item.Tag is string tag
            && int.TryParse(tag, out var fps))
            return fps;
        return 60;
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
