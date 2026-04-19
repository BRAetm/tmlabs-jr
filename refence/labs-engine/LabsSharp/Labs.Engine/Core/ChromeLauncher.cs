using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace Labs.Engine.Core;

/// <summary>Launches Chrome instances with CDP remote debugging enabled, one per session.</summary>
public class ChromeLauncher : IDisposable
{
    private const string XboxCloudUrl  = "https://www.xbox.com/en-US/play";
    private const int    CdpBasePort   = 9222;
    private static readonly string UserDataRoot = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LabsEngine");

    private static readonly string[] ChromeCandidatePaths =
    {
        @"C:\Program Files\Google\Chrome\Application\chrome.exe",
        @"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                     @"Google\Chrome\Application\chrome.exe"),
    };

    private readonly Dictionary<int, Process> _processes = new();
    private readonly object _lock = new();

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------

    /// <summary>Returns the CDP debugging port for the given session ID.</summary>
    public static int GetDebugPort(int sessionId) => CdpBasePort + sessionId;

    /// <summary>Launches a Chrome instance with CDP remote debugging.</summary>
    public async Task LaunchAsync(int sessionId, string url = XboxCloudUrl)
    {
        lock (_lock)
        {
            if (_processes.TryGetValue(sessionId, out var existing) && !existing.HasExited)
            {
                Console.WriteLine($"[ChromeLauncher] Session {sessionId}: Chrome already running (PID {existing.Id}).");
                return;
            }
        }

        var chromePath = FindChrome();
        if (chromePath is null)
            throw new InvalidOperationException("Google Chrome not found. Install Chrome or add it to the expected path.");

        var userDataDir = Path.Combine(UserDataRoot, $"Session{sessionId}");
        Directory.CreateDirectory(userDataDir);

        var args = string.Join(" ",
            $"--remote-debugging-port={GetDebugPort(sessionId)}",
            $"--user-data-dir=\"{userDataDir}\"",
            "--no-first-run",
            "--no-default-browser-check",
            "--disable-background-networking",
            $"\"{url}\"");

        var psi = new ProcessStartInfo
        {
            FileName               = chromePath,
            Arguments              = args,
            UseShellExecute        = false,
            CreateNoWindow         = false,
        };

        var process = Process.Start(psi)
            ?? throw new InvalidOperationException($"Failed to start Chrome for session {sessionId}.");

        lock (_lock)
            _processes[sessionId] = process;

        Console.WriteLine($"[ChromeLauncher] Session {sessionId}: Chrome started (PID {process.Id}, port {GetDebugPort(sessionId)}).");

        // Give Chrome time to start its CDP endpoint
        await Task.Delay(1500);
    }

    /// <summary>Returns the main window handle via Process.MainWindowHandle.</summary>
    public nint GetWindowHandle(int sessionId)
    {
        lock (_lock)
        {
            if (!_processes.TryGetValue(sessionId, out var proc) || proc.HasExited) return 0;
            proc.Refresh();
            return proc.MainWindowHandle;
        }
    }

    /// <summary>Returns the PID of the Chrome process for the given session, or 0 if not found.</summary>
    public int GetProcessId(int sessionId)
    {
        lock (_lock)
        {
            if (_processes.TryGetValue(sessionId, out var proc) && !proc.HasExited)
                return proc.Id;
        }

        // Launcher may have exited if Chrome reused an existing instance.
        // Find the real Chrome PID by looking for the process listening on our CDP port.
        return FindPidByTcpPort(GetDebugPort(sessionId));
    }

    /// <summary>Finds the PID of a process listening on the given TCP port via netstat.</summary>
    private static int FindPidByTcpPort(int port)
    {
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName               = "netstat",
                Arguments              = "-ano",
                RedirectStandardOutput = true,
                UseShellExecute        = false,
                CreateNoWindow         = true,
            };
            using var proc = Process.Start(psi);
            if (proc is null) return 0;
            var output = proc.StandardOutput.ReadToEnd();
            proc.WaitForExit(3000);

            var portStr = $":{port}";
            foreach (var line in output.Split('\n'))
            {
                if (!line.Contains("LISTENING")) continue;
                if (!line.Contains(portStr)) continue;

                var parts = line.Trim().Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length >= 5 && int.TryParse(parts[^1], out int pid) && pid > 0)
                    return pid;
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[ChromeLauncher] FindPidByTcpPort({port}) failed: {ex.Message}");
        }
        return 0;
    }

    /// <summary>Finds a Chrome window owned by a specific PID via EnumWindows.</summary>
    public static nint FindChromeWindowByPid(int pid)
    {
        if (pid == 0) return 0;
        nint result = 0;

        EnumWindows((hwnd, _) =>
        {
            if (!IsWindowVisible(hwnd)) return true;

            GetWindowThreadProcessId(hwnd, out uint windowPid);
            if (windowPid != (uint)pid) return true;

            var sb = new StringBuilder(512);
            if (GetWindowText(hwnd, sb, sb.Capacity) == 0) return true;

            // Accept any visible window owned by this Chrome process
            if (sb.ToString().Contains("Google Chrome", StringComparison.OrdinalIgnoreCase) ||
                sb.Length > 0) // Chrome's main window may not always have "Google Chrome" in title initially
            {
                result = hwnd;
                return false;
            }
            return true;
        }, 0);

        return result;
    }

    /// <summary>Finds a Chrome window via EnumWindows — fallback when Process.MainWindowHandle fails.</summary>
    public static nint FindChromeWindowByEnum()
    {
        nint result = 0;

        EnumWindows((hwnd, _) =>
        {
            if (!IsWindowVisible(hwnd)) return true;

            var sb = new StringBuilder(512);
            if (GetWindowText(hwnd, sb, sb.Capacity) == 0) return true;
            var title = sb.ToString();

            // Chrome windows have " - Google Chrome" suffix
            if (!title.Contains("Google Chrome", StringComparison.OrdinalIgnoreCase))
                return true;

            // Look for Xbox Cloud Gaming content
            if (title.Contains("Xbox", StringComparison.OrdinalIgnoreCase) ||
                title.Contains("xbox.com", StringComparison.OrdinalIgnoreCase) ||
                title.Contains("Cloud Gaming", StringComparison.OrdinalIgnoreCase) ||
                title.Contains("Play", StringComparison.OrdinalIgnoreCase))
            {
                result = hwnd;
                return false; // stop
            }

            return true;
        }, 0);

        // If no Xbox-specific window found, grab any visible Chrome window
        if (result == 0)
        {
            EnumWindows((hwnd, _) =>
            {
                if (!IsWindowVisible(hwnd)) return true;

                var sb = new StringBuilder(512);
                if (GetWindowText(hwnd, sb, sb.Capacity) == 0) return true;

                if (sb.ToString().Contains("Google Chrome", StringComparison.OrdinalIgnoreCase))
                {
                    result = hwnd;
                    return false;
                }
                return true;
            }, 0);
        }

        return result;
    }

    /// <summary>Kills the Chrome instance for the given session.</summary>
    public void StopSession(int sessionId)
    {
        Process? process;
        lock (_lock)
        {
            _processes.TryGetValue(sessionId, out process);
            _processes.Remove(sessionId);
        }

        if (process is null) return;

        try
        {
            if (!process.HasExited)
                process.Kill();
            process.Dispose();
            Console.WriteLine($"[ChromeLauncher] Session {sessionId}: Chrome stopped.");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[ChromeLauncher] Session {sessionId}: stop failed — {ex.Message}");
        }
    }

    /// <summary>Stops all running Chrome instances.</summary>
    public void Dispose()
    {
        List<int> ids;
        lock (_lock)
            ids = new List<int>(_processes.Keys);

        foreach (var id in ids)
            StopSession(id);
    }

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    private static string? FindChrome()
    {
        foreach (var path in ChromeCandidatePaths)
            if (File.Exists(path)) return path;
        return null;
    }

    // ---------------------------------------------------------------------------
    // P/Invoke
    // ---------------------------------------------------------------------------

    private delegate bool EnumWindowsProc(nint hwnd, nint lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, nint lParam);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(nint hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(nint hWnd, StringBuilder text, int count);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint hWnd, out uint lpdwProcessId);
}
