using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Labs.Engine.Core;

// ---------------------------------------------------------------------------
// Enums & Data Model
// ---------------------------------------------------------------------------

/// <summary>Lifecycle status of a single cloud gaming session.</summary>
public enum SessionStatus { Idle, Connecting, Connected, Running, Error }

/// <summary>Screen region used for MSS frame capture.</summary>
public record CaptureRegion(int X, int Y, int Width, int Height);

/// <summary>All state for one Xbox Cloud Gaming session.</summary>
public class CloudSession
{
    /// <summary>Unique session index 0–9.</summary>
    public int SessionId { get; init; }

    /// <summary>Win32 window handle for the browser window.</summary>
    public nint Hwnd { get; set; }

    /// <summary>Screen region to capture for this session.</summary>
    public CaptureRegion CaptureRegion { get; set; } = new(0, 0, 0, 0);

    /// <summary>Current lifecycle status — mutated only via SessionManager.SetStatus().</summary>
    public SessionStatus Status { get; set; } = SessionStatus.Idle;

    /// <summary>ZeroMQ port assigned to this session (base port + session ID).</summary>
    public int ZmqPort { get; init; }

    /// <summary>Browser process name that hosts this Xbox Cloud tab (e.g. "Chrome").</summary>
    public string BrowserName { get; init; } = string.Empty;

    /// <summary>Full window title of the browser tab.</summary>
    public string WindowTitle { get; init; } = string.Empty;

    /// <summary>Input source type for this session.</summary>
    public InputSourceType SourceType { get; set; } = InputSourceType.WebView2;
}

// ---------------------------------------------------------------------------
// SessionManager
// ---------------------------------------------------------------------------

/// <summary>Manages discovery, lifecycle, and state of all cloud gaming sessions.</summary>
public class SessionManager : IDisposable
{
    private const int MaxSessions  = 10;
    private const int ZmqBasePort  = 5560;
    private const int IdleClickIntervalMs = 20_000; // 20-second anti-idle period

    // Browser-specific top offsets to crop past tabs + address bar + toolbars
    private const int ChromeTopOffset  = 130; // tabs + address bar + bookmarks bar
    private const int EdgeTopOffset    = 95;  // tabs + address bar
    private const int FirefoxTopOffset = 100; // tabs + address bar
    private const int DefaultTopOffset = 95;

    private const string XboxCloudTitleKeyword = "Xbox";
    private const string XcloudTitleKeyword    = "xcloud";

    private readonly Dictionary<int, CloudSession> _sessions = new();
    private readonly object _sessionsLock = new();
    private readonly Timer  _idleClickTimer;

    // ---------------------------------------------------------------------------
    // Construction
    // ---------------------------------------------------------------------------

    public SessionManager()
    {
        _idleClickTimer = new Timer(_ => SendIdleClicksToAllSessions(),
                                    null,
                                    Timeout.Infinite,
                                    IdleClickIntervalMs);
    }

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------

    /// <summary>Enumerates Xbox Cloud Gaming windows across Chrome, Edge, and Firefox and assigns session IDs 0–5.</summary>
    public async Task<IReadOnlyList<CloudSession>> DiscoverSessionsAsync()
    {
        var found = await Task.Run(() =>
        {
            var candidates = new List<(nint Hwnd, string Title, string Browser)>();

            EnumWindows((hwnd, _) =>
            {
                if (TryGetXboxCloudInfo(hwnd, out var title, out var browser))
                    candidates.Add((hwnd, title, browser));
                return true;
            }, nint.Zero);

            return candidates;
        });

        lock (_sessionsLock)
        {
            _sessions.Clear();

            int id = 0;
            foreach (var (hwnd, title, browser) in found)
            {
                if (id >= MaxSessions) break;
                var session = CreateSession(id, hwnd, title, browser);
                _sessions[id] = session;
                LogTransition(id, null, SessionStatus.Idle);
                id++;
            }

            // Start idle clicker only while at least one session is active
            if (_sessions.Count > 0)
                _idleClickTimer.Change(IdleClickIntervalMs, IdleClickIntervalMs);
            else
                _idleClickTimer.Change(Timeout.Infinite, Timeout.Infinite);

            return new List<CloudSession>(_sessions.Values).AsReadOnly();
        }
    }

    /// <summary>Returns the session with the given ID, or null if not found.</summary>
    public CloudSession? GetSession(int id)
    {
        lock (_sessionsLock)
            return _sessions.TryGetValue(id, out var s) ? s : null;
    }

    /// <summary>Returns a snapshot of all currently tracked sessions.</summary>
    public IReadOnlyList<CloudSession> GetAllSessions()
    {
        lock (_sessionsLock)
            return new List<CloudSession>(_sessions.Values).AsReadOnly();
    }

    /// <summary>Stops the idle click timer and releases resources.</summary>
    public void Dispose() => _idleClickTimer.Dispose();

    // ---------------------------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------------------------

    /// <summary>
    /// Returns true if hwnd is an Xbox Cloud Gaming browser tab.
    /// Populates title and browser name on success.
    /// </summary>
    private bool TryGetXboxCloudInfo(nint hwnd, out string title, out string browser)
    {
        title   = string.Empty;
        browser = string.Empty;

        var sb = new StringBuilder(512);
        if (GetWindowText(hwnd, sb, sb.Capacity) == 0)
            return false;

        title = sb.ToString();

        var hasXboxKeyword = title.Contains(XboxCloudTitleKeyword,  StringComparison.OrdinalIgnoreCase)
                          || title.Contains(XcloudTitleKeyword, StringComparison.OrdinalIgnoreCase);
        if (!hasXboxKeyword)
            return false;

        browser = GetBrowserName(hwnd);
        return !string.IsNullOrEmpty(browser);
    }

    /// <summary>Resolves a friendly browser name from the process owning the given hwnd.</summary>
    private static string GetBrowserName(nint hwnd)
    {
        GetWindowThreadProcessId(hwnd, out uint pid);
        if (pid == 0) return string.Empty;

        try
        {
            using var proc = Process.GetProcessById((int)pid);
            return proc.ProcessName.ToLowerInvariant() switch
            {
                "chrome"  => "Chrome",
                "msedge"  => "Edge",
                "firefox" => "Firefox",
                _         => string.Empty,
            };
        }
        catch
        {
            return string.Empty;
        }
    }

    /// <summary>
    /// Derives the game viewport capture region by taking the client area origin
    /// and subtracting a browser-specific top offset to skip past tabs, address bar,
    /// and toolbars. Does not move, focus, or send any input to the browser window.
    /// </summary>
    private static CaptureRegion GetCaptureRegion(nint hwnd, string browserName)
    {
        if (!GetClientRect(hwnd, out RECT client))
            return new CaptureRegion(0, 0, 0, 0);

        var origin = new POINT { X = 0, Y = 0 };
        ClientToScreen(hwnd, ref origin);

        int topOffset = browserName switch
        {
            "Chrome"  => ChromeTopOffset,
            "Edge"    => EdgeTopOffset,
            "Firefox" => FirefoxTopOffset,
            _         => DefaultTopOffset,
        };

        int clientWidth  = client.Right  - client.Left;
        int clientHeight = client.Bottom - client.Top;

        int x      = origin.X;
        int y      = origin.Y + topOffset;
        int width  = clientWidth;
        int height = clientHeight - topOffset;

        if (width <= 0 || height <= 0)
            return new CaptureRegion(0, 0, 0, 0);

        return new CaptureRegion(x, y, width, height);
    }

    /// <summary>Builds a new CloudSession for the given ID and window handle with status Idle.</summary>
    private static CloudSession CreateSession(int id, nint hwnd, string windowTitle, string browserName) => new()
    {
        SessionId     = id,
        Hwnd          = hwnd,
        CaptureRegion = GetCaptureRegion(hwnd, browserName),
        Status        = SessionStatus.Idle,
        ZmqPort       = ZmqBasePort + id,
        BrowserName   = browserName,
        WindowTitle   = windowTitle,
    };

    /// <summary>Applies a status transition to a session and logs it. All status mutations go through here.</summary>
    private void SetStatus(int sessionId, SessionStatus status)
    {
        lock (_sessionsLock)
        {
            if (!_sessions.TryGetValue(sessionId, out var session))
                return;
            var previous = session.Status;
            session.Status = status;
            LogTransition(sessionId, previous, status);
        }
    }

    /// <summary>Logs every session state transition for debugging.</summary>
    private static void LogTransition(int sessionId, SessionStatus? from, SessionStatus to)
    {
        var fromLabel = from.HasValue ? from.Value.ToString() : "(new)";
        Console.WriteLine($"[SessionManager] Session {sessionId}: {fromLabel} → {to}");
    }

    // ---------------------------------------------------------------------------
    // Idle click dismisser
    // ---------------------------------------------------------------------------

    /// <summary>
    /// Sends a single passive left-click to the center of each active session's
    /// capture region every 20 seconds to keep the Xbox Cloud session alive.
    /// Uses SendInput so the click lands at the exact game viewport center.
    /// </summary>
    private void SendIdleClicksToAllSessions()
    {
        List<CloudSession> snapshot;
        lock (_sessionsLock)
            snapshot = new List<CloudSession>(_sessions.Values);

        int screenWidth  = GetSystemMetrics(SmCxscreen);
        int screenHeight = GetSystemMetrics(SmCyscreen);
        if (screenWidth == 0 || screenHeight == 0) return;

        INPUT[] inputBuf = new INPUT[3];

        foreach (var session in snapshot)
        {
            var r = session.CaptureRegion;
            if (r.Width <= 0 || r.Height <= 0) continue;

            int centerX = r.X + r.Width  / 2;
            int centerY = r.Y + r.Height / 2;

            // Normalize to 0–65535 absolute coordinate space
            int absX = centerX * 65535 / screenWidth;
            int absY = centerY * 65535 / screenHeight;

            Span<INPUT> inputs = inputBuf;

            // Move to absolute position
            inputs[0] = new INPUT
            {
                type = InputMouse,
                mi   = new MOUSEINPUT { dx = absX, dy = absY,
                                        dwFlags = MouseeventfMove | MouseeventfAbsolute }
            };
            // Left button down
            inputs[1] = new INPUT
            {
                type = InputMouse,
                mi   = new MOUSEINPUT { dwFlags = MouseeventfLeftdown }
            };
            // Left button up
            inputs[2] = new INPUT
            {
                type = InputMouse,
                mi   = new MOUSEINPUT { dwFlags = MouseeventfLeftup }
            };

            SendInput(3, ref inputs[0], Marshal.SizeOf<INPUT>());
            Console.WriteLine($"[SessionManager] Session {session.SessionId}: idle click sent to ({centerX},{centerY}).");
        }
    }

    // ---------------------------------------------------------------------------
    // P/Invoke
    // ---------------------------------------------------------------------------

    private delegate bool EnumWindowsProc(nint hwnd, nint lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, nint lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(nint hWnd, StringBuilder text, int count);

    [DllImport("user32.dll")]
    private static extern bool GetClientRect(nint hWnd, out RECT rect);

    [DllImport("user32.dll")]
    private static extern bool ClientToScreen(nint hWnd, ref POINT point);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint hWnd, out uint lpdwProcessId);

    [DllImport("user32.dll")]
    private static extern uint SendInput(uint nInputs, ref INPUT pInputs, int cbSize);

    [DllImport("user32.dll")]
    private static extern int GetSystemMetrics(int nIndex);

    // GetSystemMetrics indices
    private const int SmCxscreen = 0;
    private const int SmCyscreen = 1;

    // SendInput constants
    private const uint InputMouse         = 0;
    private const uint MouseeventfMove    = 0x0001;
    private const uint MouseeventfLeftdown = 0x0002;
    private const uint MouseeventfLeftup   = 0x0004;
    private const uint MouseeventfAbsolute = 0x8000;

    [StructLayout(LayoutKind.Sequential)]
    private struct INPUT
    {
        public uint       type;
        public MOUSEINPUT mi;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MOUSEINPUT
    {
        public int  dx, dy;
        public uint mouseData;
        public uint dwFlags;
        public uint time;
        public nint dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT { public int X, Y; }
}
