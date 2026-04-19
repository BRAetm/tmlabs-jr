using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media.Imaging;
using Labs.Engine.Core;
using Labs.Engine.Discovery;
using Labs.Engine.Input;
using Labs.Engine.Psn;
using Labs.Engine.Registration;
using Labs.Engine.Scripting;
using Labs.Engine.Session;
using Labs.Native;

namespace Labs.RemotePlay.Panels;

public partial class SessionView : UserControl
{
    private readonly ObservableCollection<DiscoveredHost> _hosts = new();
    private HostDiscovery _discovery = new();
    private DiscoveredHost? _selected;

    private readonly FramePublisher _pub;
    private StreamSession? _session;
    private ControllerInput? _input;
    private ScriptDriver? _driver;
    private int _pendingBlit;

    public ScriptDriver? Driver => _driver;
    public event Action? SessionEnded;

    /// <summary>Mute/unmute the session's audio output.</summary>
    public void SetMuted(bool muted) => _session?.SetMuted(muted);

    public SessionView(FramePublisher pub)
    {
        _pub = pub;
        InitializeComponent();
        HostList.ItemsSource = _hosts;
        _discovery.HostFound += OnHostFound;
        Loaded += (_, _) => { RefreshAccountStatus(); _ = _discovery.StartAsync(); };
        Unloaded += (_, _) => TearDown();
    }

    private void TearDown()
    {
        try { _discovery.Dispose(); } catch { }
        try { _input?.Dispose(); } catch { }
        var s = _session; _session = null;
        if (s != null) System.Threading.Tasks.Task.Run(() => { try { s.Dispose(); } catch { } });
    }

    private async void Rescan_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _hosts.Clear();
            EmptyHint.Visibility = Visibility.Visible;
            StatusBox.Text = "searching the LAN…";
            try { _discovery.Dispose(); } catch { }
            _discovery = new HostDiscovery();
            _discovery.HostFound += OnHostFound;
            await _discovery.StartAsync();
        }
        catch (Exception ex)
        {
            StatusBox.Text = $"scan failed: {ex.Message}";
        }
    }

    private void OnHostFound(DiscoveredHost host)
    {
        Dispatcher.Invoke(() =>
        {
            _hosts.Add(host);
            StatusBox.Text = $"found {_hosts.Count} console(s)";
            EmptyHint.Visibility = Visibility.Collapsed;
        });
    }

    private void HostList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        _selected = HostList.SelectedItem as DiscoveredHost;
        if (_selected == null)
        {
            SelectedName.Text = "no console selected";
            SelectedDetail.Text = "pick one from the list or wait for discovery";
            RegisterButton.IsEnabled = false;
            ConnectButton.IsEnabled = false;
            return;
        }
        SelectedName.Text = _selected.Name;
        SelectedDetail.Text = $"{_selected.Address} · status: {_selected.Status}";
        RegisterButton.IsEnabled = true;
        ConnectButton.IsEnabled = RegistrationStore.IsRegistered(_selected.HostId);
    }

    private void PsnSignIn_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new PsnLoginDialog { Owner = Window.GetWindow(this) };
        if (dlg.ShowDialog() == true) RefreshAccountStatus();
    }

    private void Register_Click(object sender, RoutedEventArgs e)
    {
        if (_selected == null) return;
        if (!RequirePsn()) return;
        var dlg = new RegisterDialog(_selected) { Owner = Window.GetWindow(this) };
        if (dlg.ShowDialog() == true) ConnectButton.IsEnabled = true;
    }

    private void Connect_Click(object sender, RoutedEventArgs e)
    {
        if (_selected == null) return;
        if (!RegistrationStore.IsRegistered(_selected.HostId))
        {
            if (!RequirePsn()) return;
            var dlg = new RegisterDialog(_selected) { Owner = Window.GetWindow(this) };
            if (dlg.ShowDialog() != true) return;
        }
        StartStream(_selected);
    }

    private void StartStream(DiscoveredHost host)
    {
        try
        {
            var creds = RegistrationStore.Load(host.HostId)
                        ?? throw new InvalidOperationException("host not registered");
            _session = new StreamSession(host, creds);
            _session.FrameReady += OnFrame;
            _session.StatusChanged += s => Dispatcher.Invoke(() => StreamOverlay.Text = s);
            _session.Start();
            _input = new ControllerInput(_session.NativeSession);

            const int sessionId = 2; // PS RP gets its own id, isolated from capture-card (1)
            _driver = new ScriptDriver(sessionId, new PsRpSink(sessionId, _input));

            PickerPanel.Visibility = Visibility.Collapsed;
            StreamPanel.Visibility = Visibility.Visible;
            Focusable = true; Focus(); // capture keyboard for controller
        }
        catch (Exception ex)
        {
            StreamOverlay.Text = "error: " + ex.Message;
        }
    }

    private void Stop_Click(object sender, RoutedEventArgs e) => StopStream();

    private void StopStream()
    {
        var d = _driver; _driver = null;
        if (d != null) _ = d.DisposeAsync();
        _input?.Dispose(); _input = null;
        var s = _session; _session = null;
        if (s != null) System.Threading.Tasks.Task.Run(() => { try { s.Dispose(); } catch { } });
        StreamPanel.Visibility = Visibility.Collapsed;
        PickerPanel.Visibility = Visibility.Visible;
        SessionEnded?.Invoke();
    }

    private static byte[] BgraToJpeg(int width, int height, byte[] bgra, int stride)
    {
        var bmp = BitmapSource.Create(width, height, 96, 96,
            System.Windows.Media.PixelFormats.Bgra32, null, bgra, stride);
        bmp.Freeze();
        var encoder = new JpegBitmapEncoder { QualityLevel = 80 };
        encoder.Frames.Add(BitmapFrame.Create(bmp));
        using var ms = new MemoryStream();
        encoder.Save(ms);
        return ms.ToArray();
    }

    private void OnFrame(int width, int height, byte[] bgra, int stride)
    {
        // Copy the buffer immediately — native code may recycle it
        byte[] copy;
        try
        {
            copy = new byte[bgra.Length];
            Buffer.BlockCopy(bgra, 0, copy, 0, bgra.Length);
        }
        catch { return; }

        // Publish to CV pipeline via SHM on background thread — zero JPEG encode,
        // Python gets a numpy view straight off the mmap.
        System.Threading.Tasks.Task.Run(() =>
        {
            try { _pub.PublishBgra(2, width, height, stride, copy); } catch { }
        });

        if (System.Threading.Interlocked.Exchange(ref _pendingBlit, 1) == 1) return;
        Dispatcher.BeginInvoke(new Action(() =>
        {
            try
            {
                var bmp = BitmapSource.Create(width, height, 96, 96,
                    System.Windows.Media.PixelFormats.Bgra32, null, copy, stride);
                bmp.Freeze();
                VideoSurface.Source = bmp;
                StreamOverlay.Text = "";
            }
            catch { }
            finally { System.Threading.Interlocked.Exchange(ref _pendingBlit, 0); }
        }));
    }

    protected override void OnKeyDown(KeyEventArgs e) { HandleKey(e, true); base.OnKeyDown(e); }
    protected override void OnKeyUp(KeyEventArgs e)   { HandleKey(e, false); base.OnKeyUp(e); }

    private void HandleKey(KeyEventArgs e, bool down)
    {
        if (_input == null || StreamPanel.Visibility != Visibility.Visible) return;
        switch (e.Key)
        {
            case Key.X:        _input.SetButton(LabsNative.LabsControllerButton.CROSS, down); break;
            case Key.Z:        _input.SetButton(LabsNative.LabsControllerButton.MOON, down); break;
            case Key.C:        _input.SetButton(LabsNative.LabsControllerButton.BOX, down); break;
            case Key.V:        _input.SetButton(LabsNative.LabsControllerButton.PYRAMID, down); break;
            case Key.Up:       _input.SetButton(LabsNative.LabsControllerButton.DPAD_UP, down); break;
            case Key.Down:     _input.SetButton(LabsNative.LabsControllerButton.DPAD_DOWN, down); break;
            case Key.Left:     _input.SetButton(LabsNative.LabsControllerButton.DPAD_LEFT, down); break;
            case Key.Right:    _input.SetButton(LabsNative.LabsControllerButton.DPAD_RIGHT, down); break;
            case Key.Enter:    _input.SetButton(LabsNative.LabsControllerButton.OPTIONS, down); break;
            case Key.RightShift:
            case Key.LeftShift: _input.SetButton(LabsNative.LabsControllerButton.SHARE, down); break;
            case Key.Space:    _input.SetButton(LabsNative.LabsControllerButton.PS, down); break;
            case Key.Q:        _input.SetButton(LabsNative.LabsControllerButton.L1, down); break;
            case Key.E:        _input.SetButton(LabsNative.LabsControllerButton.R1, down); break;
            case Key.F:        _input.SetTriggers(down ? (byte)255 : (byte)0, 0); break;
            case Key.G:        _input.SetTriggers(0, down ? (byte)255 : (byte)0); break;
            case Key.W:        _input.SetLeftStick(0, down ? (short)-32767 : (short)0); break;
            case Key.S:        _input.SetLeftStick(0, down ? (short)32767 : (short)0); break;
            case Key.A:        _input.SetLeftStick(down ? (short)-32767 : (short)0, 0); break;
            case Key.D:        _input.SetLeftStick(down ? (short)32767 : (short)0, 0); break;
        }
    }

    private bool RequirePsn()
    {
        if (AccountStore.HasAccount) return true;
        MessageBox.Show(Window.GetWindow(this), "Sign in with PSN first.",
            "PS Remote Play", MessageBoxButton.OK, MessageBoxImage.Information);
        return false;
    }

    private void RefreshAccountStatus()
    {
        AccountStatus.Text = AccountStore.HasAccount ? "signed in ✓" : "not signed in";
        PsnSignInButton.Content = AccountStore.HasAccount ? "re-sign in" : "sign in";
    }
}
