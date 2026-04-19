using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Labs.Engine.Core;

namespace Labs.Engine.Scripting;

/// <summary>
/// Per-session script lifecycle: spawns a CvWorkerHost for one session,
/// routes its GamepadState updates to a single IGamepadSink. Each session view
/// owns its own ScriptDriver so the pipelines stay isolated.
/// </summary>
public sealed class ScriptDriver : IAsyncDisposable
{
    private readonly int _sessionId;
    private readonly int _zmqPort;
    private readonly IGamepadSink _sink;
    private readonly CvWorkerHost _host = new();
    private string? _activeScript;

    public bool IsRunning => _activeScript != null;
    public string? ActiveScript => _activeScript;
    public IGamepadSink Sink => _sink;
    public int SessionId => _sessionId;

    public event Action<string>? Status;

    public ScriptDriver(int sessionId, IGamepadSink sink, int zmqPort = 5560)
    {
        _sessionId = sessionId;
        _zmqPort = zmqPort + sessionId;
        _sink = sink;
        _host.StateReceived += (sid, state) =>
        {
            if (sid == _sessionId) _sink.SendInput(_sessionId, state);
        };
        _host.WorkerDied += (_, code) => Status?.Invoke($"worker died (exit {code})");
        _host.WorkerRestarted += (_, why) => Status?.Invoke("restarted: " + why);
    }

    public static string ScriptsRoot => Path.Combine(AppContext.BaseDirectory, "Scripts");

    /// <summary>Additional script roots, checked alongside the bundled Scripts/ folder.</summary>
    public static System.Collections.Generic.IEnumerable<string> ExtraScriptRoots()
    {
        // Repo-local cv-scripts — lets users drop .py files next to the repo without
        // rebuilding. Walks up from the exe dir to find a sibling cv-scripts folder.
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (int i = 0; i < 6 && dir != null; i++, dir = dir.Parent)
        {
            var candidate = Path.Combine(dir.FullName, "cv-scripts");
            if (Directory.Exists(candidate)) { yield return candidate; yield break; }
        }
    }

    /// <summary>Returns .py files available across all script roots (excluding worker.py/support modules).</summary>
    public static System.Collections.Generic.List<string> AvailableScripts()
    {
        var results = new System.Collections.Generic.List<string>();
        var roots = new System.Collections.Generic.List<string> { ScriptsRoot };
        roots.AddRange(ExtraScriptRoots());
        foreach (var root in roots)
        {
            try
            {
                if (!Directory.Exists(root)) continue;
                foreach (var p in Directory.EnumerateFiles(root, "*.py", SearchOption.AllDirectories))
                {
                    var name = Path.GetFileName(p);
                    if (name.Equals("worker.py", StringComparison.OrdinalIgnoreCase)) continue;
                    if (name.StartsWith("labs_frame_shm", StringComparison.OrdinalIgnoreCase)) continue;
                    if (name.StartsWith("labs_gamepad_shm", StringComparison.OrdinalIgnoreCase)) continue;
                    results.Add(p);
                }
            }
            catch { }
        }
        return results;
    }

    public async Task StartAsync(string scriptPath)
    {
        await StopAsync();
        var session = new CloudSession
        {
            SessionId = _sessionId,
            ZmqPort = _zmqPort,
            SourceType = InputSourceType.WebView2,
            WindowTitle = $"labs-session-{_sessionId}",
            BrowserName = "labs",
        };
        try
        {
            await _host.StartAsync(session, scriptPath);
            _activeScript = scriptPath;
            Status?.Invoke($"running · {Path.GetFileName(scriptPath)}");
        }
        catch (Exception ex)
        {
            Status?.Invoke("start failed: " + ex.Message);
            throw;
        }
    }

    public async Task StopAsync()
    {
        if (_activeScript == null) return;
        try { await _host.StopAsync(_sessionId); } catch { }
        _activeScript = null;
        Status?.Invoke("stopped");
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync();
        try { await _host.DisposeAsync(); } catch { }
    }
}
