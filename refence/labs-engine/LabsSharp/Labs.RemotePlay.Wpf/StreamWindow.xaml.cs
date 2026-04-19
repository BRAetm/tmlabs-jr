using System;
using System.Windows;
using System.Windows.Input;
using Labs.Engine.Discovery;
using Labs.Engine.Input;
using Labs.Engine.Registration;
using Labs.Engine.Session;
using Labs.Native;

namespace Labs.RemotePlay;

public partial class StreamWindow : Window
{
    private readonly DiscoveredHost _host;
    private StreamSession? _session;
    private ControllerInput? _input;

    public StreamWindow(DiscoveredHost host)
    {
        InitializeComponent();
        _host = host;
        Loaded += OnLoaded;
        Closed += OnClosed;
        KeyDown += OnKey;
        KeyUp += OnKey;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            var creds = RegistrationStore.Load(_host.HostId)
                        ?? throw new InvalidOperationException("Host is not registered.");
            _session = new StreamSession(_host, creds);
            _session.FrameReady += OnFrame;
            _session.StatusChanged += s => Dispatcher.Invoke(() => Overlay.Text = s);
            _session.Start();
            _input = new ControllerInput(_session.NativeSession);
        }
        catch (Exception ex)
        {
            Overlay.Text = "Error: " + ex.Message;
        }
    }

    private int _pendingBlit; // 0 = idle, 1 = a frame is being handed to WPF

    private void OnFrame(int width, int height, byte[] bgra, int stride)
    {
        // Coalesce: drop the frame if a blit is already pending. Protects WPF
        // from being flooded when decode runs faster than the UI thread.
        if (System.Threading.Interlocked.Exchange(ref _pendingBlit, 1) == 1) return;

        Dispatcher.BeginInvoke(new Action(() =>
        {
            try
            {
                var bmp = System.Windows.Media.Imaging.BitmapSource.Create(
                    width, height, 96, 96,
                    System.Windows.Media.PixelFormats.Bgra32,
                    null, bgra, stride);
                bmp.Freeze();
                VideoSurface.Source = bmp;
                Overlay.Text = "";
            }
            finally { System.Threading.Interlocked.Exchange(ref _pendingBlit, 0); }
        }));
    }

    private void OnKey(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Escape) { Close(); return; }
        if (e.Key == Key.F11) { WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized; return; }
        if (_input == null) return;
        var down = e.IsDown;
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
            case Key.F:        _input.SetTriggers(down ? (byte)255 : (byte)0, 0); break;   // L2
            case Key.G:        _input.SetTriggers(0, down ? (byte)255 : (byte)0); break;   // R2
            case Key.W:        _input.SetLeftStick(0, down ? (short)-32767 : (short)0); break;
            case Key.S:        _input.SetLeftStick(0, down ? (short)32767 : (short)0); break;
            case Key.A:        _input.SetLeftStick(down ? (short)-32767 : (short)0, 0); break;
            case Key.D:        _input.SetLeftStick(down ? (short)32767 : (short)0, 0); break;
        }
        e.Handled = true;
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        _input?.Dispose();
        // Tear down the session on a background thread. labs_session_join and
        // the decode thread can each take up to a few seconds; doing that on
        // the UI thread paints the main window as "Not Responding".
        var session = _session;
        _session = null;
        if (session != null)
        {
            System.Threading.Tasks.Task.Run(() =>
            {
                try { session.Dispose(); } catch { }
            });
        }
    }
}
