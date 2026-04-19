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

    private static readonly string ScriptsFolderConfigPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "LabsEngine", "scripts_folder.txt");

    private string? _customScriptsFolder;

    private string ResolvedScriptsFolder
    {
        get
        {
            // User-chosen override wins if still present on disk.
            if (!string.IsNullOrWhiteSpace(_customScriptsFolder) && Directory.Exists(_customScriptsFolder))
                return _customScriptsFolder;

            var resolved = Path.GetFullPath(ScriptsFolder);
            if (!Directory.Exists(resolved))
            {
                resolved = Path.Combine(AppContext.BaseDirectory, "cv-scripts");
                Directory.CreateDirectory(resolved);
            }
            return resolved;
        }
    }

    private void LoadCustomScriptsFolder()
    {
        try
        {
            if (File.Exists(ScriptsFolderConfigPath))
                _customScriptsFolder = File.ReadAllText(ScriptsFolderConfigPath).Trim();
        }
        catch { /* best-effort */ }
    }

    private void SaveCustomScriptsFolder()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(ScriptsFolderConfigPath)!);
            File.WriteAllText(ScriptsFolderConfigPath, _customScriptsFolder ?? "");
        }
        catch { /* best-effort */ }
    }

    private void ChangeScriptsFolder_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new Microsoft.Win32.OpenFolderDialog
        {
            Title = "choose scripts folder",
            InitialDirectory = ResolvedScriptsFolder,
        };
        if (dlg.ShowDialog(this) != true) return;
        _customScriptsFolder = dlg.FolderName;
        SaveCustomScriptsFolder();
        RefreshScriptsList();
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
            MonitorBig?.Update(state);
            // WebView2 sessions need gamepad injected via the shim (browser Gamepad API is focus-gated).
            if (SessionHost.Child is XboxRpView) _currentFanOut?.SendInput(_currentSessionId, state);
        };
        _xinput.ConnectionChanged += connected =>
            Dispatcher.Invoke(() =>
            {
                AppendLog(connected ? "xinput: controller connected" : "xinput: controller disconnected");
                if (MonitorStatus != null)
                    MonitorStatus.Text = connected ? "xinput: controller connected" : "xinput: waiting for controller";
                if (!connected) { Monitor.Clear(); MonitorBig?.Clear(); }
            });

        _gpc.OutputReceived += line => Dispatcher.Invoke(() => AppendLog(line));
        _gpc.VmExited += code => Dispatcher.Invoke(() => { AppendLog($"gpc3: vm exited ({code})"); UpdateGpcStatus(); });

        LoadCustomScriptsFolder();
        Loaded += (_, _) => { RefreshScriptsList(); UpdateDeviceStatus(); ApplyBackgroundImageFromTheme(); };
        Labs.RemotePlay.Theme.ThemeService.ThemeApplied += () => Dispatcher.Invoke(ApplyBackgroundImageFromTheme);

        var t = new System.Windows.Threading.DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(500),
        };
        t.Tick += (_, _) => BindBridgeToActiveDriver();
        t.Start();

        var fpsTimer = new System.Windows.Threading.DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(300),
        };
        fpsTimer.Tick += (_, _) => UpdateFpsPill();
        fpsTimer.Start();
    }

    private void Tab_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button b || b.Tag is not string id) return;
        SelectTab(id);
    }

    private void SelectTab(string id)
    {
        VideoTab.Visibility   = id == "video"   ? Visibility.Visible : Visibility.Collapsed;
        MonitorTab.Visibility = id == "monitor" ? Visibility.Visible : Visibility.Collapsed;
        MarketTab.Visibility  = id == "market"  ? Visibility.Visible : Visibility.Collapsed;

        var active = (Style)FindResource("TabBtnActive");
        var muted  = (Style)FindResource("TabBtn");
        TabVideo.Style   = id == "video"   ? active : muted;
        TabMonitor.Style = id == "monitor" ? active : muted;
        TabMarket.Style  = id == "market"  ? active : muted;
    }

    private void UpdateFpsPill()
    {
        StopSessionBtn.Visibility = SessionHost.Child is not null ? Visibility.Visible : Visibility.Collapsed;

        var d = GetActiveDriver();
        var streaming = d is not null;
        StatsHud.Visibility          = streaming ? Visibility.Visible : Visibility.Collapsed;
        ControllerOverlay.Visibility = (streaming && SessionHost.Child is not XboxRpView) ? Visibility.Visible : Visibility.Collapsed;

        if (d is null)
        {
            FpsPill.Visibility = Visibility.Collapsed;
            return;
        }
        var fps = _framePublisher.GetFps(d.SessionId);
        FpsPill.Visibility = Visibility.Visible;
        FpsText.Text = $"{fps:F0} fps";

        // Helios-style HUD — same data, different surface. Display FPS mirrors processed FPS
        // until we track separate render-side metrics.
        ProcFpsText.Text = $" {fps:F0} fps";
        DispFpsText.Text = $" {fps:F0} fps";
    }

    private void OpenTheme_Click(object sender, RoutedEventArgs e)
    {
        var w = new ThemeWindow { Owner = this };
        w.ShowDialog();
        ApplyBackgroundImageFromTheme();
    }

    private void ApplyBackgroundImageFromTheme()
    {
        var brush = Labs.RemotePlay.Theme.ThemeService.BuildBackgroundImageBrush();
        if (brush is not null) Background = brush;
        else Background = (System.Windows.Media.Brush)Application.Current.Resources["Bg"];
    }

    private void MoreMenu_Click(object sender, RoutedEventArgs e)
    {
        if (sender is Button b && b.ContextMenu is not null)
        {
            b.ContextMenu.PlacementTarget = b;
            b.ContextMenu.Placement = System.Windows.Controls.Primitives.PlacementMode.Bottom;
            b.ContextMenu.IsOpen = true;
        }
    }

    private void StopSession_Click(object sender, RoutedEventArgs e) => StopActiveSession();

    private void StopActiveSession()
    {
        if (SessionHost.Child is null) return;
        // Clearing the child removes it from the visual tree, triggering each view's
        // Unloaded handler (CaptureView disposes capture loop, SessionView stops stream, etc).
        SessionHost.Child = null;
        _activeSession = null;
        SessionHost.Visibility = Visibility.Collapsed;
        EmptyStage.Visibility = Visibility.Visible;
        AppendLog("session: stopped");
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
            if (_currentFanOut is not null) _currentFanOut.StateObserved -= OnMonitorState;
            fanOut.StateObserved += OnMonitorState;
            _currentFanOut = fanOut;
            _gamepadBridge.SetActiveSink(fanOut, d.SessionId);
            _gamepadShm.SetActiveSink(fanOut, d.SessionId);
        }
        else
        {
            if (_currentSink is null) return;
            _currentSink = null;
            _currentSessionId = 0;
            if (_currentFanOut is not null) { _currentFanOut.StateObserved -= OnMonitorState; _currentFanOut = null; }
            _gamepadBridge.SetActiveSink(null, 0);
            _gamepadShm.SetActiveSink(null, 0);
            Monitor.Clear();
            MonitorBig?.Clear();
        }
    }

    // Fan-out subscriber that mirrors input to both the compact overlay and the full-tab monitor.
    private void OnMonitorState(GamepadState s)
    {
        Monitor.Update(s);
        MonitorBig?.Update(s);
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

        UpdateScriptsPathDisplay(folder, scripts.Count);
        AppendLog($"scripts: scanned {folder} — {scripts.Count} script(s)");
    }

    private void UpdateScriptsPathDisplay(string folder, int count)
    {
        var home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        string display = folder.StartsWith(home, StringComparison.OrdinalIgnoreCase)
            ? "~" + folder.Substring(home.Length).Replace('\\', '/')
            : folder.Replace('\\', '/');
        ScriptsPathText.Text = display;
        ScriptsPathText.ToolTip = $"{folder}\n{count} script(s)";
    }

    private void ScriptCombo_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (ScriptCombo.SelectedItem is ComboBoxItem item)
        {
            var name = item.Content?.ToString();
            _selectedScript = name == "(none)" ? null : name;
        }
    }

    private ScriptDriver? GetActiveDriver()
    {
        return SessionHost.Child switch
        {
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
        LogScroller?.ScrollToEnd();
    }

    private void ClearLog_Click(object sender, RoutedEventArgs e) => LogOutput.Text = "";

    private void CaptionMin_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void CaptionMax_Click(object sender, RoutedEventArgs e)
    {
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
        if (MaxBtn != null) MaxBtn.Content = WindowState == WindowState.Maximized ? "❐" : "◻";
    }

    private void CaptionClose_Click(object sender, RoutedEventArgs e) => Close();

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
        var view = new XboxRpView(_framePublisher);
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
        SessionHost.Child = null;
        _activeSession = null;
        SessionHost.Visibility = Visibility.Collapsed;
        EmptyStage.Visibility = Visibility.Visible;
        AppendLog("session: ended");
    }
}
