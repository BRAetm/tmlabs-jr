using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using Labs.Engine.Core;
using Labs.Engine.Core.Shm;
using Labs.Engine.Scripting;
using Labs.RemotePlay.Panels;

namespace Labs.RemotePlay;

public partial class MainWindow : Window
{
    private readonly FramePublisher _framePublisher = new();
    private readonly GamepadHttpBridge _gamepadBridge = new();
    private readonly LabsGamepadShmReader _gamepadShm = new();
    private readonly HeliosCompatShmBridge _heliosCompat = new();
    private readonly GpcRunner _gpc = new();
    private readonly TitanBridge _titan = new();
    private readonly XInputReader _xinput = new();
    private SessionView? _activeSession;
    private bool _isMuted;
    private string? _selectedScript;
    private IGamepadSink? _currentSink;
    private int _currentSessionId;
    private FanOutSink? _currentFanOut;

    private static readonly string ScriptsFolder = Path.Combine(
        Path.GetDirectoryName(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar))
            ?? AppContext.BaseDirectory,
        "..", "..", "..", "..", "cv-scripts");

    private string ResolvedScriptsFolder
    {
        get
        {
            var resolved = Path.GetFullPath(ScriptsFolder);
            if (!Directory.Exists(resolved))
            {
                resolved = Path.Combine(AppContext.BaseDirectory, "cv-scripts");
                Directory.CreateDirectory(resolved);
            }
            return resolved;
        }
    }

    public MainWindow()
    {
        InitializeComponent();
        _framePublisher.Start();
        _gamepadBridge.Start();
        _gamepadShm.Start();

        // Forward GPC3 VM output (a script modifying controller state) to whichever
        // sink is currently bound to the live session.
        _heliosCompat.OutputReceived += state =>
        {
            var sink = _currentSink;
            if (sink is null) return;
            try { sink.SendInput(_currentSessionId, state); }
            catch (Exception ex) { Dispatcher.Invoke(() => AppendLog($"gpc3 sink error: {ex.Message}")); }

            // Also forward to Titan if connected — lets a GPC3 script drive a
            // physical Titan Two in parallel with the ViGEm pad.
            if (_titan.IsConnected) { try { _titan.SendState(state); } catch { } }
        };

        _xinput.StateChanged += state =>
        {
            Monitor.Update(state);
            // Forward to active Xbox RP so gamepad keeps working when window loses focus.
            if (SessionHost.Child is XboxRemotePlayView xboxRp) xboxRp.ForwardPhysicalInput(state);
        };
        _xinput.ConnectionChanged += connected =>
            Dispatcher.Invoke(() => { AppendLog(connected ? "xinput: controller connected" : "xinput: controller disconnected"); if (!connected) Monitor.Clear(); });

        _gpc.OutputReceived += line => Dispatcher.Invoke(() => AppendLog(line));
        _gpc.VmExited += code => Dispatcher.Invoke(() => { AppendLog($"gpc3: vm exited ({code})"); UpdateGpcStatus(); });

        Loaded += (_, _) => { RefreshScriptsList(); UpdateDeviceStatus(); };

        var t = new System.Windows.Threading.DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(500),
        };
        t.Tick += (_, _) => BindBridgeToActiveDriver();
        t.Start();
    }

    protected override void OnClosed(EventArgs e)
    {
        try { _ = _gpc.StopAsync(); } catch { }
        try { _xinput.Dispose(); } catch { }
        try { _titan.Dispose(); } catch { }
        try { _heliosCompat.Dispose(); } catch { }
        _gamepadShm.Dispose();
        _gamepadBridge.Dispose();
        _framePublisher.Dispose();
        base.OnClosed(e);
    }

    private void BindBridgeToActiveDriver()
    {
        var d = GetActiveDriver();
        if (d != null)
        {
            // Same primary sink? already bound — nothing to do. Avoids rebuilding
            // the FanOut every 500ms tick and leaking Monitor subscriptions.
            if (ReferenceEquals(_currentSink, d.Sink)) return;

            _currentSink = d.Sink;
            _currentSessionId = d.SessionId;
            // Fan-out sink: primary (ViGEm/remote play) + helios SHM mirror for GPC3 + optional Titan + UI monitor.
            var fanOut = new FanOutSink(d.Sink, _heliosCompat, () => _titan.IsConnected ? _titan : null);
            if (_currentFanOut is not null) _currentFanOut.StateObserved -= Monitor.Update;
            fanOut.StateObserved += Monitor.Update;
            _currentFanOut = fanOut;
            _gamepadBridge.SetActiveSink(fanOut, d.SessionId);
            _gamepadShm.SetActiveSink(fanOut, d.SessionId);
        }
        else
        {
            if (_currentSink is null) return;
            _currentSink = null;
            _currentSessionId = 0;
            if (_currentFanOut is not null) { _currentFanOut.StateObserved -= Monitor.Update; _currentFanOut = null; }
            _gamepadBridge.SetActiveSink(null, 0);
            _gamepadShm.SetActiveSink(null, 0);
            Monitor.Clear();
        }
    }

    private void RefreshScriptsList()
    {
        var folder = ResolvedScriptsFolder;
        var scripts = Directory.Exists(folder)
            ? Directory.EnumerateFiles(folder)
                .Where(f => f.EndsWith(".py", StringComparison.OrdinalIgnoreCase)
                         || f.EndsWith(".gpc", StringComparison.OrdinalIgnoreCase)
                         || f.EndsWith(".gpc3", StringComparison.OrdinalIgnoreCase))
                .Select(Path.GetFileName).OrderBy(n => n).ToList()
            : new();

        ScriptCombo.Items.Clear();
        ScriptCombo.Items.Add(new ComboBoxItem { Content = "(none)", IsSelected = true });
        foreach (var s in scripts)
            ScriptCombo.Items.Add(new ComboBoxItem { Content = s });

        ScriptList.Items.Clear();
        foreach (var s in scripts)
        {
            var item = new ListBoxItem { Content = s, FontFamily = new("Cascadia Mono, Consolas"), FontSize = 12 };
            ScriptList.Items.Add(item);
        }

        if (scripts.Count == 0)
        {
            ScriptList.Items.Add(new ListBoxItem
            {
                Content = "no scripts found",
                FontSize = 11,
                Foreground = FindResource("TextDim") as System.Windows.Media.Brush,
                IsHitTestVisible = false
            });
        }

        AppendLog($"scripts: scanned {folder} — {scripts.Count} script(s)");
    }

    private void ScriptCombo_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (ScriptCombo.SelectedItem is ComboBoxItem item)
        {
            var name = item.Content?.ToString();
            _selectedScript = name == "(none)" ? null : name;
        }
    }

    private void ScriptList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ScriptList.SelectedItem is ListBoxItem item && item.IsHitTestVisible)
        {
            var name = item.Content?.ToString();
            for (int i = 0; i < ScriptCombo.Items.Count; i++)
            {
                if (ScriptCombo.Items[i] is ComboBoxItem ci && ci.Content?.ToString() == name)
                {
                    ScriptCombo.SelectedIndex = i;
                    break;
                }
            }
        }
    }

    private ScriptDriver? GetActiveDriver()
    {
        return SessionHost.Child switch
        {
            XboxRemotePlayView v => v.Driver,
            XboxRpView v => v.Driver,
            XboxView v => v.Driver,
            CaptureView v => v.Driver,
            SessionView v => v.Driver,
            _ => null
        };
    }

    private async void RunScript_Click(object sender, RoutedEventArgs e)
    {
        if (_selectedScript == null)
        {
            AppendLog("run: no script selected");
            return;
        }
        var driver = GetActiveDriver();
        if (driver == null)
        {
            AppendLog("run: no active session — start a session first");
            return;
        }
        var path = Path.Combine(ResolvedScriptsFolder, _selectedScript);
        if (!File.Exists(path))
        {
            AppendLog($"run: {_selectedScript} not found");
            return;
        }

        var ext = Path.GetExtension(path).ToLowerInvariant();
        if (ext is ".gpc" or ".gpc3")
        {
            if (!GpcRunner.IsAvailable())
            {
                AppendLog("run: gpc3 tools not installed (need gpc3vmc.exe + gpc3vm-run.exe under Tools/gpc3)");
                return;
            }
            AppendLog($"run: compiling {_selectedScript} with gpc3vmc …");
            var ok = await _gpc.CompileAndRunAsync(path);
            AppendLog(ok ? $"run: {_selectedScript} running on gpc3 vm (pid bridge {_heliosCompat.HostPid})"
                         : $"run: {_selectedScript} failed to start on gpc3 vm");
            UpdateGpcStatus();
            return;
        }

        try
        {
            await driver.StartAsync(path);
            AppendLog($"run: {_selectedScript} started");
        }
        catch (Exception ex)
        {
            AppendLog($"run: failed — {ex.Message}");
        }
    }

    private async void StopScript_Click(object sender, RoutedEventArgs e)
    {
        if (_gpc.IsRunning)
        {
            try { await _gpc.StopAsync(); AppendLog("stop: gpc3 vm stopped"); UpdateGpcStatus(); }
            catch (Exception ex) { AppendLog($"stop: gpc3 failed — {ex.Message}"); }
            return;
        }

        var driver = GetActiveDriver();
        if (driver == null || !driver.IsRunning)
        {
            AppendLog("stop: no script running");
            return;
        }
        try
        {
            await driver.StopAsync();
            AppendLog("stop: script stopped");
        }
        catch (Exception ex)
        {
            AppendLog($"stop: failed — {ex.Message}");
        }
    }

    private void Mute_Click(object sender, RoutedEventArgs e)
    {
        _isMuted = !_isMuted;
        MuteBtn.Content = _isMuted ? "unmute" : "mute";
        _activeSession?.SetMuted(_isMuted);
        if (SessionHost.Child is XboxRemotePlayView xbox) xbox.SetMuted(_isMuted);
        AppendLog(_isMuted ? "audio: muted" : "audio: unmuted");
    }

    private void RefreshScripts_Click(object sender, RoutedEventArgs e)
    {
        RefreshScriptsList();
    }

    private void OpenScriptsFolder_Click(object sender, RoutedEventArgs e)
    {
        var folder = ResolvedScriptsFolder;
        Directory.CreateDirectory(folder);
        Process.Start(new ProcessStartInfo("explorer.exe", folder) { UseShellExecute = true });
    }

    private void TitanScan_Click(object sender, RoutedEventArgs e)
    {
        var devices = TitanBridge.ScanDevices();
        if (devices.Count == 0)
        {
            TitanDeviceStatus.Text = "not found";
            TitanDeviceStatus.Foreground = (System.Windows.Media.Brush)FindResource("TextDim");
            AppendLog("titan: no devices found");
            return;
        }
        TitanDeviceStatus.Text = $"{devices.Count} found";
        TitanDeviceStatus.Foreground = (System.Windows.Media.Brush)FindResource("Success");
        foreach (var d in devices) AppendLog($"titan: {d.Name} ({d.Type})");
    }

    private void TitanConnect_Click(object sender, RoutedEventArgs e)
    {
        if (_titan.IsConnected)
        {
            _titan.Disconnect();
            TitanStatusText.Text = "not connected";
            TitanConnectBtn.Content = "connect titan";
            UpdateDeviceStatus();
            AppendLog("titan: disconnected");
            return;
        }
        if (_titan.ConnectAuto())
        {
            TitanStatusText.Text = _titan.ConnectedDevice?.Name ?? "connected";
            TitanConnectBtn.Content = "disconnect";
            UpdateDeviceStatus();
            AppendLog($"titan: connected to {_titan.ConnectedDevice?.Name}");
        }
        else
        {
            AppendLog("titan: connect failed — is the device plugged in?");
        }
    }

    private void UpdateDeviceStatus()
    {
        TitanDeviceStatus.Text = _titan.IsConnected ? "connected" : (TitanBridge.IsAnyDeviceAvailable() ? "available" : "not found");
        TitanDeviceStatus.Foreground = _titan.IsConnected
            ? (System.Windows.Media.Brush)FindResource("Success")
            : (System.Windows.Media.Brush)FindResource("TextDim");
        UpdateGpcStatus();
    }

    private void UpdateGpcStatus()
    {
        if (_gpc.IsRunning) { GpcStatusText.Text = "running"; GpcStatusText.Foreground = (System.Windows.Media.Brush)FindResource("Success"); }
        else if (GpcRunner.IsAvailable()) { GpcStatusText.Text = "available"; GpcStatusText.Foreground = (System.Windows.Media.Brush)FindResource("Success"); }
        else { GpcStatusText.Text = "missing tools"; GpcStatusText.Foreground = (System.Windows.Media.Brush)FindResource("TextDim"); }
    }

    private void AppendLog(string msg)
    {
        var stamp = DateTime.Now.ToString("HH:mm:ss");
        LogOutput.Text += $"[{stamp}] {msg}\n";
    }

    private void Back_Click(object sender, RoutedEventArgs e)
    {
        var launcher = new LauncherWindow();
        Application.Current.MainWindow = launcher;
        launcher.Show();
        Close();
    }

    private void NewSession_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new NewSessionDialog { Owner = this };
        if (dlg.ShowDialog() != true) return;
        switch (dlg.Chosen)
        {
            case NewSessionDialog.SessionKind.PsRemotePlay:
                StartPsRemotePlayInline();
                break;
            case NewSessionDialog.SessionKind.CaptureCard:
                StartCaptureCardInline();
                break;
            case NewSessionDialog.SessionKind.XboxRemotePlay:
                StartXboxRpInline();
                break;
            case NewSessionDialog.SessionKind.CloudGaming:
                StartWebInline("https://www.xbox.com/play", "xCloud", 4);
                break;
        }
    }

    private void StartWebInline(string url, string label, int sessionId)
    {
        EndSession();
        var view = new XboxView(url, label, sessionId, _framePublisher);
        view.SessionEnded += EndSession;
        SessionHost.Child = view;
        SessionHost.Visibility = Visibility.Visible;
        EmptyStage.Visibility = Visibility.Collapsed;
        AppendLog($"session: {label} started (id={sessionId})");
    }

    private void StartXboxRpInline()
    {
        EndSession();
        var view = new XboxRemotePlayView(_framePublisher);
        view.SessionEnded += EndSession;
        SessionHost.Child = view;
        SessionHost.Visibility = Visibility.Visible;
        EmptyStage.Visibility = Visibility.Collapsed;
        AppendLog("session: Xbox Remote Play started (id=3)");
    }

    private void StartCaptureCardInline()
    {
        EndSession();
        var view = new CaptureView(_framePublisher);
        view.SessionEnded += EndSession;
        SessionHost.Child = view;
        SessionHost.Visibility = Visibility.Visible;
        EmptyStage.Visibility = Visibility.Collapsed;
        AppendLog("session: capture card started");
    }

    private void StartPsRemotePlayInline()
    {
        EndSession();
        _activeSession = new SessionView(_framePublisher);
        _activeSession.SessionEnded += EndSession;
        SessionHost.Child = _activeSession;
        SessionHost.Visibility = Visibility.Visible;
        EmptyStage.Visibility = Visibility.Collapsed;
        AppendLog("session: PS Remote Play started");
    }

    private void EndSession()
    {
        if (_activeSession == null)
        {
            SessionHost.Visibility = Visibility.Collapsed;
            EmptyStage.Visibility = Visibility.Visible;
            return;
        }
        SessionHost.Child = null;
        _activeSession = null;
        SessionHost.Visibility = Visibility.Collapsed;
        EmptyStage.Visibility = Visibility.Visible;
        AppendLog("session: ended");
    }
}
