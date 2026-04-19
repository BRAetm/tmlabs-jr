using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using NetMQ;
using NetMQ.Sockets;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Labs.Engine.Core;

/// <summary>Spawns and manages Python CV worker subprocesses per session.</summary>
public class CvWorkerHost : IAsyncDisposable
{
    private const string PythonExecutable = "python";
    private static readonly string WorkerScript = Path.Combine(AppContext.BaseDirectory, "Scripts", "worker.py");
    private const int MaxAutoRestarts = 3;
    private const int RestartBaseDelayMs = 1000;

    private readonly Dictionary<int, WorkerEntry> _workers = new();
    private readonly Dictionary<int, FileSystemWatcher> _scriptWatchers = new();
    private readonly Dictionary<int, int> _restartCounts = new();
    private readonly object _workersLock = new();

    // ---------------------------------------------------------------------------
    // Events
    // ---------------------------------------------------------------------------

    /// <summary>Raised whenever a GamepadState update is received from any worker session.</summary>
    public event Action<int, GamepadState>? StateReceived;

    /// <summary>Raised when a CV-annotated JPEG frame is received from a worker (sessionId, jpegBytes).</summary>
    public event Action<int, byte[]>? CvFrameReceived;

    /// <summary>Raised when a worker process exits unexpectedly (sessionId, exitCode).</summary>
    public event Action<int, int>? WorkerDied;

    /// <summary>Raised when a worker is auto-restarted (sessionId, reason).</summary>
    public event Action<int, string>? WorkerRestarted;

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------

    /// <summary>Spawns the Python worker subprocess for the given session and starts the ZMQ pull loop.</summary>
    public async Task StartAsync(CloudSession session, string scriptPath)
    {
        Log($"[CvWorkerHost] Session {session.SessionId}: StartAsync called. Script={scriptPath}, ZmqPort={session.ZmqPort}");

        // Validate paths before spawning
        if (!File.Exists(WorkerScript))
        {
            Log($"[CvWorkerHost] Session {session.SessionId}: FATAL — worker.py not found at {WorkerScript}");
            throw new FileNotFoundException($"worker.py not found at {WorkerScript}");
        }
        if (!File.Exists(scriptPath))
        {
            Log($"[CvWorkerHost] Session {session.SessionId}: FATAL — script not found at {scriptPath}");
            throw new FileNotFoundException($"Script not found at {scriptPath}");
        }

        // Stop any existing worker for this session first (prevents port leaks)
        await StopAsync(session.SessionId);

        var cts = new CancellationTokenSource();

        // Start the pull loop on a dedicated thread — it creates & owns the PULL socket
        // Use a signal so we wait for the socket to bind before spawning Python
        var socketReady = new TaskCompletionSource<bool>();
        var pullLoop = RunPullLoopAsync(session.SessionId, session.ZmqPort, cts.Token, socketReady);

        // Wait for the PULL socket to bind (or fail)
        try
        {
            await socketReady.Task;
        }
        catch (Exception ex)
        {
            Log($"[CvWorkerHost] Session {session.SessionId}: PULL socket bind FAILED — {ex.Message}");
            cts.Cancel();
            cts.Dispose();
            throw;
        }

        var psi = BuildWorkerProcess(session, scriptPath);
        var process = new Process { StartInfo = psi, EnableRaisingEvents = true };

        process.OutputDataReceived += (_, e) => { if (e.Data is not null) OnWorkerOutput(session.SessionId, e.Data); };
        process.ErrorDataReceived  += (_, e) => { if (e.Data is not null) OnWorkerOutput(session.SessionId, e.Data); };

        try
        {
            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
        }
        catch (Exception ex)
        {
            Log($"[CvWorkerHost] Session {session.SessionId}: process.Start() FAILED — {ex.Message}");
            cts.Cancel();
            cts.Dispose();
            throw;
        }

        Log($"[CvWorkerHost] Session {session.SessionId}: worker started (PID {process.Id}, port {session.ZmqPort}), cmd: {psi.FileName} {psi.Arguments}");

        var entry = new WorkerEntry(session.SessionId, session, scriptPath, process, pullLoop, cts);

        lock (_workersLock)
        {
            _workers[session.SessionId] = entry;
            _restartCounts[session.SessionId] = 0;
        }

        // Set up file watcher for hot-reload
        WatchScriptFile(session.SessionId, session, scriptPath);

        // Check if process died immediately
        await Task.Delay(500);
        if (process.HasExited)
        {
            var exitCode = process.ExitCode;
            Log($"[CvWorkerHost] Session {session.SessionId}: worker DIED immediately (exit code {exitCode})");
            WorkerDied?.Invoke(session.SessionId, exitCode);
        }
        else
        {
            _ = MonitorWorkerHealthAsync(session.SessionId);
        }
    }

    /// <summary>Stops the ZMQ pull loop, terminates the Python subprocess, and cleans up resources.</summary>
    public async Task StopAsync(int sessionId)
    {
        WorkerEntry? entry;
        lock (_workersLock)
        {
            _workers.TryGetValue(sessionId, out entry);
            _workers.Remove(sessionId);
        }

        if (entry is null)
        {
            Log($"[CvWorkerHost] Session {sessionId}: StopAsync — no worker found.");
            return;
        }

        await ShutdownEntryAsync(entry);
    }

    /// <summary>Stops all running workers and disposes all resources.</summary>
    public async ValueTask DisposeAsync()
    {
        List<WorkerEntry> entries;
        lock (_workersLock)
        {
            entries = new List<WorkerEntry>(_workers.Values);
            _workers.Clear();
        }

        foreach (var entry in entries)
            await ShutdownEntryAsync(entry);
    }

    // ---------------------------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------------------------

    /// <summary>Cancels pull loop, kills process, disposes socket and file watcher for a single worker entry.</summary>
    private async Task ShutdownEntryAsync(WorkerEntry entry)
    {
        StopWatchingScript(entry.SessionId);
        entry.Cts.Cancel();

        try { await entry.PullLoop.ConfigureAwait(false); }
        catch { /* expected — cancelled or faulted */ }

        try
        {
            if (!entry.Process.HasExited)
                entry.Process.Kill();
        }
        catch (Exception ex) { Log($"[CvWorkerHost] Session {entry.SessionId}: failed to kill process — {ex.Message}"); }

        try { entry.Process.Dispose(); } catch { }
        try { entry.Cts.Dispose(); } catch { }

        Log($"[CvWorkerHost] Session {entry.SessionId}: worker stopped.");
    }

    // ---------------------------------------------------------------------------
    // Script hot-reload via FileSystemWatcher
    // ---------------------------------------------------------------------------

    /// <summary>Watches the script file for changes and auto-restarts the worker.</summary>
    private void WatchScriptFile(int sessionId, CloudSession session, string scriptPath)
    {
        StopWatchingScript(sessionId);

        try
        {
            var dir = Path.GetDirectoryName(scriptPath);
            var file = Path.GetFileName(scriptPath);
            if (dir is null || file is null) return;

            var watcher = new FileSystemWatcher(dir, file)
            {
                NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size,
                EnableRaisingEvents = true
            };

            // Debounce: scripts often trigger multiple write events
            DateTime lastReload = DateTime.MinValue;
            watcher.Changed += async (_, e) =>
            {
                if ((DateTime.Now - lastReload).TotalSeconds < 2) return;
                lastReload = DateTime.Now;

                Log($"[CvWorkerHost] Session {sessionId}: script file changed, hot-reloading...");
                try
                {
                    await StopAsync(sessionId);
                    await Task.Delay(500); // Let ports release
                    await StartAsync(session, scriptPath);
                    WorkerRestarted?.Invoke(sessionId, "hot-reload");
                    Log($"[CvWorkerHost] Session {sessionId}: hot-reload complete.");
                }
                catch (Exception ex)
                {
                    Log($"[CvWorkerHost] Session {sessionId}: hot-reload FAILED — {ex.Message}");
                }
            };

            lock (_workersLock)
                _scriptWatchers[sessionId] = watcher;

            Log($"[CvWorkerHost] Session {sessionId}: watching {file} for changes.");
        }
        catch (Exception ex)
        {
            Log($"[CvWorkerHost] Session {sessionId}: file watcher setup failed — {ex.Message}");
        }
    }

    /// <summary>Stops watching script file changes for the given session.</summary>
    private void StopWatchingScript(int sessionId)
    {
        lock (_workersLock)
        {
            if (_scriptWatchers.Remove(sessionId, out var watcher))
            {
                watcher.EnableRaisingEvents = false;
                watcher.Dispose();
            }
        }
    }

    private static readonly string LogFile = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "cvccloud", "worker_debug.log");

    /// <summary>Builds ProcessStartInfo for: python worker.py {sessionId} {zmqPort} {scriptPath}</summary>
    private static ProcessStartInfo BuildWorkerProcess(CloudSession session, string scriptPath)
    {
        var psi = new ProcessStartInfo
        {
            FileName               = PythonExecutable,
            Arguments              = $"-u \"{WorkerScript}\" {session.SessionId} {session.ZmqPort} \"{scriptPath}\"",
            UseShellExecute        = false,
            RedirectStandardOutput = true,
            RedirectStandardError  = true,
            CreateNoWindow         = true,
        };
        // Force Python to run unbuffered so we see output immediately
        psi.Environment["PYTHONUNBUFFERED"] = "1";
        return psi;
    }

    /// <summary>Background thread: creates SUB socket, binds it, receives gamepad events from Python PUB.</summary>
    private Task RunPullLoopAsync(int sessionId, int zmqPort, CancellationToken ct, TaskCompletionSource<bool> socketReady)
    {
        // NetMQ sockets are NOT thread-safe — create, bind, and use on the SAME dedicated thread.
        return Task.Factory.StartNew(() =>
        {
            SubscriberSocket? sub = null;
            Exception? lastBindError = null;
            const int bindAttempts = 8;
            for (int attempt = 1; attempt <= bindAttempts; attempt++)
            {
                try
                {
                    sub?.Dispose();
                    sub = new SubscriberSocket();
                    sub.Bind($"tcp://127.0.0.1:{zmqPort}");
                    sub.Subscribe($"gamepad_{sessionId}");
                    sub.Subscribe($"cv_frame_{sessionId}");
                    Log($"[CvWorkerHost] Session {sessionId}: SUB socket bound on port {zmqPort} (attempt {attempt}), topics 'gamepad_{sessionId}' + 'cv_frame_{sessionId}', thread {Environment.CurrentManagedThreadId}.");
                    lastBindError = null;
                    break;
                }
                catch (NetMQ.AddressAlreadyInUseException ex)
                {
                    lastBindError = ex;
                    Log($"[CvWorkerHost] Session {sessionId}: port {zmqPort} busy (attempt {attempt}/{bindAttempts}) — retrying in 250ms.");
                    if (ct.IsCancellationRequested) break;
                    Thread.Sleep(250);
                }
                catch (Exception ex)
                {
                    lastBindError = ex;
                    break;
                }
            }
            if (lastBindError is not null || sub is null)
            {
                socketReady.SetException(lastBindError ?? new Exception("Failed to bind SUB socket"));
                sub?.Dispose();
                return;
            }
            socketReady.SetResult(true);

            int rxCount = 0;
            int missCount = 0;

            while (!ct.IsCancellationRequested)
            {
                try
                {
                    if (!CheckProcessHealth(sessionId, out var exitCode))
                    {
                        Log($"[CvWorkerHost] Session {sessionId}: sub loop exiting — worker dead (code {exitCode}).");
                        WorkerDied?.Invoke(sessionId, exitCode);
                        break;
                    }

                    // Receive multipart: [topic] [payload]
                    if (sub.TryReceiveFrameString(TimeSpan.FromMilliseconds(100), out var topic) && topic is not null)
                    {
                        if (topic.StartsWith("cv_frame_"))
                        {
                            // CV annotated frame — binary JPEG
                            if (sub.TryReceiveFrameBytes(TimeSpan.FromMilliseconds(50), out var jpegBytes) && jpegBytes is not null)
                            {
                                missCount = 0;
                                CvFrameReceived?.Invoke(sessionId, jpegBytes);
                            }
                        }
                        else if (sub.TryReceiveFrameString(TimeSpan.FromMilliseconds(50), out var json) && json is not null)
                        {
                            missCount = 0;
                            var state = ParseGamepadState(json);
                            if (state is not null)
                            {
                                StateReceived?.Invoke(sessionId, state);
                                rxCount++;
                                if (rxCount <= 5 || rxCount % 50 == 0)
                                    Log($"[CvWorkerHost] Session {sessionId}: received gamepad event #{rxCount}");
                            }
                        }
                    }
                    else
                    {
                        missCount++;
                        if (missCount == 20 || missCount % 200 == 0)
                            Log($"[CvWorkerHost] Session {sessionId}: sub miss x{missCount} (total rx={rxCount})");
                    }
                }
                catch (OperationCanceledException) { break; }
                catch (Exception ex)
                {
                    Log($"[CvWorkerHost] Session {sessionId}: sub loop ERROR — {ex.GetType().Name}: {ex.Message}");
                    Thread.Sleep(100); // prevent tight error loop
                }
            }

            sub.Dispose();
            Log($"[CvWorkerHost] Session {sessionId}: sub loop stopped (received {rxCount} total events).");
        }, ct, TaskCreationOptions.LongRunning, TaskScheduler.Default);
    }

    /// <summary>Background health monitor — auto-restarts crashed workers with exponential backoff.</summary>
    private async Task MonitorWorkerHealthAsync(int sessionId)
    {
        while (true)
        {
            await Task.Delay(2000);

            WorkerEntry? entry;
            lock (_workersLock)
            {
                if (!_workers.TryGetValue(sessionId, out entry))
                    return; // Worker was removed (normal shutdown)
            }

            if (entry.Process.HasExited)
            {
                var exitCode = entry.Process.ExitCode;
                Log($"[CvWorkerHost] Session {sessionId}: health monitor detected worker death (code {exitCode}).");

                // Attempt auto-restart with exponential backoff
                int restartCount;
                lock (_workersLock)
                    restartCount = _restartCounts.GetValueOrDefault(sessionId, 0);

                if (restartCount < MaxAutoRestarts)
                {
                    var delayMs = RestartBaseDelayMs * (1 << restartCount); // 1s, 2s, 4s
                    Log($"[CvWorkerHost] Session {sessionId}: auto-restart #{restartCount + 1}/{MaxAutoRestarts} in {delayMs}ms...");

                    lock (_workersLock)
                        _restartCounts[sessionId] = restartCount + 1;

                    await Task.Delay(delayMs);

                    try
                    {
                        await StartAsync(entry.Session, entry.ScriptPath);
                        WorkerRestarted?.Invoke(sessionId, $"auto-restart (attempt {restartCount + 1})");
                        Log($"[CvWorkerHost] Session {sessionId}: auto-restart successful.");
                        return; // New health monitor is running from StartAsync
                    }
                    catch (Exception ex)
                    {
                        Log($"[CvWorkerHost] Session {sessionId}: auto-restart FAILED — {ex.Message}");
                        WorkerDied?.Invoke(sessionId, exitCode);
                        return;
                    }
                }
                else
                {
                    Log($"[CvWorkerHost] Session {sessionId}: max restarts ({MaxAutoRestarts}) exceeded, giving up.");
                    WorkerDied?.Invoke(sessionId, exitCode);
                    return;
                }
            }
        }
    }

    /// <summary>Parses Web Gamepad API JSON (axes + buttons arrays) into a GamepadState.</summary>
    private static GamepadState? ParseGamepadState(string json)
    {
        try
        {
            var obj = JObject.Parse(json);
            var state = new GamepadState();

            var axes = obj["axes"] as JArray;
            if (axes is not null)
                for (int i = 0; i < Math.Min(axes.Count, 4); i++)
                    state.Axes[i] = axes[i]?.Value<float>() ?? 0f;

            var buttons = obj["buttons"] as JArray;
            if (buttons is not null)
                for (int i = 0; i < Math.Min(buttons.Count, 17); i++)
                    state.Buttons[i] = buttons[i]?.Value<bool>() ?? false;

            return state;
        }
        catch (Exception ex)
        {
            Log($"[CvWorkerHost] Failed to parse GamepadState: {ex.Message} | raw: {json}");
            return null;
        }
    }

    private static void Log(string msg)
    {
        Console.WriteLine(msg);
        try { File.AppendAllText(LogFile, $"[{DateTime.Now:HH:mm:ss.fff}] {msg}{Environment.NewLine}"); } catch { }
    }

    /// <summary>Logs stdout/stderr output from the Python worker subprocess.</summary>
    private static void OnWorkerOutput(int sessionId, string line) =>
        Log($"[Worker:{sessionId}] {line}");

    /// <summary>Checks whether the worker subprocess is still alive. Returns false if exited.</summary>
    private bool CheckProcessHealth(int sessionId, out int exitCode)
    {
        exitCode = -1;
        WorkerEntry? entry;
        lock (_workersLock)
            _workers.TryGetValue(sessionId, out entry);

        if (entry is not null && entry.Process.HasExited)
        {
            exitCode = entry.Process.ExitCode;
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // Private record
    // ---------------------------------------------------------------------------

    private record WorkerEntry(
        int SessionId,
        CloudSession Session,
        string ScriptPath,
        Process Process,
        Task PullLoop,
        CancellationTokenSource Cts);
}
