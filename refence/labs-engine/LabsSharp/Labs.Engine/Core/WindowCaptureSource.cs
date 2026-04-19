using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Media.Imaging;

namespace Labs.Engine.Core;

/// <summary>Info about a discovered application window.</summary>
public record AppWindowInfo(nint Hwnd, string Title, string ProcessName, int Pid);

/// <summary>
/// Captures frames from any application window.
/// Uses Windows Graphics Capture (WGC) for UWP apps (Xbox) and
/// PrintWindow (GDI) for regular apps (PS Remote Play, etc.).
/// </summary>
public class WindowCaptureSource : IFrameSource
{
    private readonly int _sessionId;
    private readonly nint _hwnd;
    private readonly string _windowTitle;

    private CancellationTokenSource? _cts;
    private Task? _captureLoop;
    private volatile bool _isCapturing;

    // WGC capture (used for UWP apps like Xbox)
    private WindowCapture? _wgcCapture;
    private volatile byte[]? _wgcLatestJpeg;

    // Shared memory bridge for zero-copy frame transfer to Python
    private static readonly Lazy<SharedFrameBridge> _sharedBridge = new(() =>
    {
        var bridge = new SharedFrameBridge();
        bridge.Start();
        return bridge;
    });

    public InputSourceType SourceType => InputSourceType.WindowCapture;
    public string SourceName => _windowTitle;
    public bool IsCapturing => _isCapturing;
    public int TargetFps { get; set; } = 60;

    public event Action<int, byte[]>? FrameReady;

    public WindowCaptureSource(int sessionId, nint hwnd, string windowTitle)
    {
        _sessionId = sessionId;
        _hwnd = hwnd;
        _windowTitle = windowTitle;
    }

    /// <summary>Checks if the target window belongs to a UWP/MSIX app that needs WGC capture.</summary>
    private bool IsUwpWindow()
    {
        try
        {
            GetWindowThreadProcessId(_hwnd, out uint pid);
            var proc = Process.GetProcessById((int)pid);
            var name = proc.ProcessName.ToLowerInvariant();
            // Xbox app, GameBar, and ApplicationFrameHost are UWP
            return name is "xboxpcapp" or "xbox" or "gamebar" or "xboxapp" or "msxboxapp"
                or "applicationframehost";
        }
        catch { return false; }
    }

    // ---------------------------------------------------------------------------
    // Window Discovery
    // ---------------------------------------------------------------------------

    /// <summary>Finds Xbox Remote Play (Xbox app) windows.</summary>
    public static List<AppWindowInfo> FindXboxRemotePlay()
    {
        // Xbox app runs as "XboxPcApp" or "GameBar", window title often contains "Xbox"
        var results = new List<AppWindowInfo>();
        var all = ListAllWindows();
        foreach (var w in all)
        {
            var proc = w.ProcessName.ToLowerInvariant();
            var title = w.Title.ToLowerInvariant();
            if (proc == "xboxpcapp" || proc == "xbox" || proc == "gamebar" ||
                proc == "xboxapp" || proc == "msxboxapp" ||
                title.Contains("xbox") || title.Contains("remote play"))
            {
                results.Add(w);
            }
        }
        return results;
    }

    /// <summary>Finds PS Remote Play (chiaki-ng or official app) windows.</summary>
    public static List<AppWindowInfo> FindPsRemotePlay()
    {
        var results = new List<AppWindowInfo>();
        var all = ListAllWindows();
        foreach (var w in all)
        {
            var proc = w.ProcessName.ToLowerInvariant();
            var title = w.Title.ToLowerInvariant();
            if (proc.Contains("chiaki") || proc.Contains("remoteplay") || proc.Contains("psremote") ||
                title.Contains("remote play") || title.Contains("chiaki") || title.Contains("playstation"))
            {
                results.Add(w);
            }
        }
        return results;
    }

    /// <summary>Finds any visible window matching the given keywords.</summary>
    public static List<AppWindowInfo> FindWindowsByKeywords(params string[] keywords)
    {
        var results = new List<AppWindowInfo>();

        EnumWindows((hwnd, _) =>
        {
            if (!IsWindowVisible(hwnd)) return true;

            var sb = new StringBuilder(512);
            GetWindowText(hwnd, sb, sb.Capacity);
            var title = sb.ToString();
            if (string.IsNullOrWhiteSpace(title)) return true;

            // Match against keywords
            bool match = false;
            foreach (var kw in keywords)
            {
                if (title.Contains(kw, StringComparison.OrdinalIgnoreCase))
                {
                    match = true;
                    break;
                }
            }

            if (!match)
            {
                // Also check process name
                GetWindowThreadProcessId(hwnd, out uint pid);
                try
                {
                    using var proc = Process.GetProcessById((int)pid);
                    foreach (var kw in keywords)
                    {
                        if (proc.ProcessName.Contains(kw, StringComparison.OrdinalIgnoreCase))
                        {
                            match = true;
                            break;
                        }
                    }

                    if (match)
                        results.Add(new AppWindowInfo(hwnd, title, proc.ProcessName, (int)pid));
                }
                catch { }
            }
            else
            {
                GetWindowThreadProcessId(hwnd, out uint pid);
                string procName = "";
                try { using var p = Process.GetProcessById((int)pid); procName = p.ProcessName; } catch { }
                results.Add(new AppWindowInfo(hwnd, title, procName, (int)pid));
            }

            return true;
        }, nint.Zero);

        return results;
    }

    /// <summary>Lists all visible windows with titles (for the "pick a window" UI).</summary>
    public static List<AppWindowInfo> ListAllWindows()
    {
        var results = new List<AppWindowInfo>();

        EnumWindows((hwnd, _) =>
        {
            if (!IsWindowVisible(hwnd)) return true;

            var sb = new StringBuilder(512);
            GetWindowText(hwnd, sb, sb.Capacity);
            var title = sb.ToString();
            if (string.IsNullOrWhiteSpace(title)) return true;

            GetWindowThreadProcessId(hwnd, out uint pid);
            string procName = "";
            try { using var p = Process.GetProcessById((int)pid); procName = p.ProcessName; } catch { }
            results.Add(new AppWindowInfo(hwnd, title, procName, (int)pid));
            return true;
        }, nint.Zero);

        return results;
    }

    // ---------------------------------------------------------------------------
    // IFrameSource
    // ---------------------------------------------------------------------------

    public Task StartAsync()
    {
        if (_isCapturing) return Task.CompletedTask;

        if (!IsWindow(_hwnd))
            throw new InvalidOperationException($"Window handle 0x{_hwnd:X} is no longer valid");

        _isCapturing = true;

        if (IsUwpWindow())
        {
            // Use WGC for UWP apps (Xbox Remote Play, etc.)
            Console.WriteLine($"[WindowCapture] Session {_sessionId}: UWP detected, using WGC for '{_windowTitle}'");
            _wgcCapture = new WindowCapture();
            _wgcCapture.FrameReady += OnWgcFrameReady;
            _wgcCapture.Start(_hwnd);

            // Start a pump thread to publish frames at target FPS
            _cts = new CancellationTokenSource();
            _captureLoop = Task.Factory.StartNew(
                () => WgcPumpLoop(_cts.Token),
                _cts.Token,
                TaskCreationOptions.LongRunning,
                TaskScheduler.Default);
        }
        else
        {
            // Use PrintWindow for regular apps (PS Remote Play, etc.)
            _cts = new CancellationTokenSource();
            _captureLoop = Task.Factory.StartNew(
                () => CaptureLoop(_cts.Token),
                _cts.Token,
                TaskCreationOptions.LongRunning,
                TaskScheduler.Default);
        }

        Console.WriteLine($"[WindowCapture] Session {_sessionId}: Started capturing '{_windowTitle}' at {TargetFps}fps");
        return Task.CompletedTask;
    }

    public Task StopAsync()
    {
        _isCapturing = false;
        _cts?.Cancel();
        try { _captureLoop?.Wait(TimeSpan.FromSeconds(2)); } catch { }
        _cts?.Dispose();
        _cts = null;

        if (_wgcCapture is not null)
        {
            _wgcCapture.FrameReady -= OnWgcFrameReady;
            _wgcCapture.Dispose();
            _wgcCapture = null;
        }

        Console.WriteLine($"[WindowCapture] Session {_sessionId}: Stopped.");
        return Task.CompletedTask;
    }

    /// <summary>Called by WGC when a new frame arrives — writes to shared memory + JPEG fallback.</summary>
    private void OnWgcFrameReady(BitmapSource frame)
    {
        try
        {
            // Write to shared memory (zero-copy path for Python scripts)
            _sharedBridge.Value.WriteFrame(frame);

            // Also encode JPEG for the C# UI preview / ZMQ fallback
            BitmapSource source = frame;
            if (frame.PixelWidth > 1280)
            {
                double scale = 960.0 / frame.PixelWidth;
                var scaled = new TransformedBitmap(frame, new System.Windows.Media.ScaleTransform(scale, scale));
                scaled.Freeze();
                source = scaled;
            }

            var encoder = new JpegBitmapEncoder { QualityLevel = 75 };
            encoder.Frames.Add(BitmapFrame.Create(source));
            using var ms = new MemoryStream(120_000);
            encoder.Save(ms);
            _wgcLatestJpeg = ms.ToArray();
        }
        catch { }
    }

    /// <summary>Publishes the latest WGC frame at the target FPS.</summary>
    private void WgcPumpLoop(CancellationToken ct)
    {
        int frameCount = 0;
        double frameInterval = 1000.0 / TargetFps;
        var sw = Stopwatch.StartNew();

        while (!ct.IsCancellationRequested)
        {
            var frameStart = sw.ElapsedMilliseconds;

            var jpeg = _wgcLatestJpeg;
            if (jpeg is not null && jpeg.Length > 0)
            {
                FrameReady?.Invoke(_sessionId, jpeg);
                frameCount++;
                if (frameCount <= 3 || frameCount % 50 == 0)
                    Console.WriteLine($"[WindowCapture] Session {_sessionId}: WGC frame #{frameCount} ({jpeg.Length} bytes)");
            }

            var elapsed = sw.ElapsedMilliseconds - frameStart;
            var sleepMs = (int)(frameInterval - elapsed);
            if (sleepMs > 0) Thread.Sleep(sleepMs);
        }

        Console.WriteLine($"[WindowCapture] Session {_sessionId}: WGC pump ended ({frameCount} frames)");
    }

    // ---------------------------------------------------------------------------
    // Capture Loop (BitBlt/PrintWindow)
    // ---------------------------------------------------------------------------

    private void CaptureLoop(CancellationToken ct)
    {
        int frameCount = 0;
        double frameInterval = 1000.0 / TargetFps;
        var sw = Stopwatch.StartNew();

        while (!ct.IsCancellationRequested)
        {
            var frameStart = sw.ElapsedMilliseconds;

            try
            {
                if (!IsWindow(_hwnd))
                {
                    Console.WriteLine($"[WindowCapture] Session {_sessionId}: Window closed.");
                    break;
                }

                var jpegBytes = CaptureWindowToJpeg(_hwnd, _sharedBridge.Value);
                if (jpegBytes is not null && jpegBytes.Length > 0)
                {
                    FrameReady?.Invoke(_sessionId, jpegBytes);
                    frameCount++;
                    if (frameCount <= 3 || frameCount % 100 == 0)
                        Console.WriteLine($"[WindowCapture] Session {_sessionId}: Frame #{frameCount} ({jpegBytes.Length} bytes)");
                }
            }
            catch (Exception ex)
            {
                if (!ct.IsCancellationRequested)
                    Console.WriteLine($"[WindowCapture] Session {_sessionId}: Error: {ex.Message}");
            }

            var elapsed = sw.ElapsedMilliseconds - frameStart;
            var sleepMs = (int)(frameInterval - elapsed);
            if (sleepMs > 0) Thread.Sleep(sleepMs);
        }

        Console.WriteLine($"[WindowCapture] Session {_sessionId}: Loop ended ({frameCount} frames)");
    }

    // Reusable JPEG encoder + params to avoid per-frame allocation
    private static readonly ImageCodecInfo? _jpegCodec = Array.Find(
        ImageCodecInfo.GetImageEncoders(), e => e.MimeType == "image/jpeg");
    private static readonly EncoderParameters _jpegParams = new(1)
    {
        Param = { [0] = new EncoderParameter(System.Drawing.Imaging.Encoder.Quality, 88L) }
    };

    /// <summary>Captures a window to JPEG bytes using PrintWindow + GDI. Also writes to shared memory.</summary>
    private static byte[]? CaptureWindowToJpeg(nint hwnd, SharedFrameBridge? bridge = null)
    {
        if (!GetClientRect(hwnd, out RECT rect)) return null;
        int width = rect.Right - rect.Left;
        int height = rect.Bottom - rect.Top;
        if (width <= 0 || height <= 0) return null;

        nint hdcWindow = GetDC(hwnd);
        nint hdcMem = CreateCompatibleDC(hdcWindow);
        nint hBitmap = CreateCompatibleBitmap(hdcWindow, width, height);
        nint hOld = SelectObject(hdcMem, hBitmap);

        // PrintWindow with PW_RENDERFULLCONTENT for DWM-composited windows
        PrintWindow(hwnd, hdcMem, 0x00000002);

        SelectObject(hdcMem, hOld);

        byte[]? result = null;
        try
        {
            using var bmp = Image.FromHbitmap(hBitmap);

            // Downscale to half res for faster encode + smaller ZMQ transfer
            Bitmap target;
            if (width > 1280)
            {
                int newW = width / 2;
                int newH = height / 2;
                target = new Bitmap(newW, newH);
                using var g = Graphics.FromImage(target);
                g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.Bilinear;
                g.DrawImage(bmp, 0, 0, newW, newH);
            }
            else
            {
                target = (Bitmap)bmp;
            }

            using var ms = new MemoryStream(120_000);
            if (_jpegCodec is not null)
                target.Save(ms, _jpegCodec, _jpegParams);
            else
                target.Save(ms, ImageFormat.Jpeg);
            result = ms.ToArray();

            if (target != bmp) target.Dispose();
        }
        catch { }

        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWindow);

        return result;
    }

    public void Dispose()
    {
        StopAsync().Wait(TimeSpan.FromSeconds(2));
    }

    // ---------------------------------------------------------------------------
    // P/Invoke
    // ---------------------------------------------------------------------------

    private delegate bool EnumWindowsProc(nint hwnd, nint lParam);

    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, nint lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(nint hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(nint hWnd);
    [DllImport("user32.dll")] private static extern bool IsWindow(nint hWnd);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(nint hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll")] private static extern bool GetClientRect(nint hWnd, out RECT lpRect);
    [DllImport("user32.dll")] private static extern nint GetDC(nint hWnd);
    [DllImport("user32.dll")] private static extern int ReleaseDC(nint hWnd, nint hDC);
    [DllImport("user32.dll")] private static extern bool PrintWindow(nint hWnd, nint hdcBlt, uint nFlags);
    [DllImport("gdi32.dll")] private static extern nint CreateCompatibleDC(nint hdc);
    [DllImport("gdi32.dll")] private static extern nint CreateCompatibleBitmap(nint hdc, int width, int height);
    [DllImport("gdi32.dll")] private static extern nint SelectObject(nint hdc, nint h);
    [DllImport("gdi32.dll")] private static extern bool DeleteDC(nint hdc);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(nint ho);

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT { public int Left, Top, Right, Bottom; }
}
