using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using OpenCvSharp;

namespace Labs.Engine.Core;

/// <summary>Info about a detected video capture device.</summary>
public record CaptureDeviceInfo(int Index, string Name);

/// <summary>
/// Captures frames from a USB capture card (Elgato, Magewell, generic) via OpenCV/DirectShow.
/// Implements IFrameSource to feed the CV pipeline — same as Helios's OpencvCapture plugin.
/// </summary>
public class CaptureCardSource : IFrameSource
{
    private readonly int _sessionId;
    private readonly int _deviceIndex;
    private readonly string _deviceName;
    private readonly int _captureWidth;
    private readonly int _captureHeight;

    private VideoCapture? _capture;
    private CancellationTokenSource? _cts;
    private Task? _captureLoop;
    private volatile bool _isCapturing;

    public InputSourceType SourceType => InputSourceType.CaptureCard;
    public string SourceName => _deviceName;
    public bool IsCapturing => _isCapturing;
    public int TargetFps { get; set; } = 120;

    public event Action<int, byte[]>? FrameReady;

    /// <summary>Creates a capture card source for the given device.</summary>
    public CaptureCardSource(int sessionId, int deviceIndex, string deviceName,
                              int width = 1920, int height = 1080)
    {
        _sessionId = sessionId;
        _deviceIndex = deviceIndex;
        _deviceName = deviceName;
        _captureWidth = width;
        _captureHeight = height;
    }

    // ---------------------------------------------------------------------------
    // Device Enumeration
    // ---------------------------------------------------------------------------

    /// <summary>Scans for available video capture devices via DirectShow, with real hardware names.</summary>
    public static List<CaptureDeviceInfo> ScanDevices(int maxDevices = 10)
    {
        var deviceNames = GetDirectShowDeviceNames();
        var devices = new List<CaptureDeviceInfo>();

        // Bound scan to the number of devices actually reported by Windows so
        // we don't poke OpenCV with a stale index — calling Set() on a
        // half-open DSHOW handle segfaults in native code.
        int scanLimit = Math.Min(maxDevices, Math.Max(deviceNames.Count, 1));

        for (int i = 0; i < scanLimit; i++)
        {
            VideoCapture? cap = null;
            try
            {
                cap = new VideoCapture(i, VideoCaptureAPIs.DSHOW);
                if (!cap.IsOpened()) continue;

                // Probe with a single frame grab — safer than Set() on a
                // device that may not be exclusively claimable.
                using var probe = new Mat();
                bool ok = false;
                try { ok = cap.Read(probe); } catch { ok = false; }
                if (!ok || probe.Empty()) continue;

                int w = probe.Width, h = probe.Height;
                string name = i < deviceNames.Count
                    ? $"{deviceNames[i]} ({w}x{h})"
                    : $"Capture Device {i} ({w}x{h})";

                devices.Add(new CaptureDeviceInfo(i, name));
                Console.WriteLine($"[CaptureCard] Found device {i}: {name}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[CaptureCard] scan #{i} threw: {ex.Message}");
            }
            finally
            {
                try { cap?.Dispose(); } catch { }
            }
        }

        return devices;
    }

    /// <summary>Gets real hardware names of DirectShow video capture devices via WMI.</summary>
    private static List<string> GetDirectShowDeviceNames()
    {
        var names = new List<string>();
        try
        {
            // Use PowerShell to query DirectShow device names — most reliable method on Windows
            var psi = new System.Diagnostics.ProcessStartInfo
            {
                FileName = "powershell",
                Arguments = "-NoProfile -Command \"Get-CimInstance Win32_PnPEntity | Where-Object { $_.Service -eq 'usbvideo' -or $_.Caption -match 'capture|webcam|camera|elgato|magewell|avermedia|HD60|cam link' } | Select-Object -ExpandProperty Caption\"",
                UseShellExecute = false,
                RedirectStandardOutput = true,
                CreateNoWindow = true,
            };

            var proc = System.Diagnostics.Process.Start(psi);
            if (proc is not null)
            {
                var output = proc.StandardOutput.ReadToEnd();
                proc.WaitForExit(5000);

                foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries))
                {
                    var trimmed = line.Trim();
                    if (!string.IsNullOrWhiteSpace(trimmed))
                        names.Add(trimmed);
                }
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[CaptureCard] WMI device name query failed: {ex.Message}");
        }

        // Fallback: try ffmpeg-style enumeration via OpenCV backend name
        if (names.Count == 0)
        {
            try
            {
                var psi = new System.Diagnostics.ProcessStartInfo
                {
                    FileName = "powershell",
                    Arguments = "-NoProfile -Command \"Get-PnpDevice -Class Camera -Status OK | Select-Object -ExpandProperty FriendlyName; Get-PnpDevice -Class Image -Status OK | Select-Object -ExpandProperty FriendlyName\"",
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    CreateNoWindow = true,
                };
                var proc = System.Diagnostics.Process.Start(psi);
                if (proc is not null)
                {
                    var output = proc.StandardOutput.ReadToEnd();
                    proc.WaitForExit(5000);
                    foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries))
                    {
                        var trimmed = line.Trim();
                        if (!string.IsNullOrWhiteSpace(trimmed))
                            names.Add(trimmed);
                    }
                }
            }
            catch { }
        }

        return names;
    }

    // ---------------------------------------------------------------------------
    // IFrameSource
    // ---------------------------------------------------------------------------

    public Task StartAsync()
    {
        if (_isCapturing) return Task.CompletedTask;

        _capture = new VideoCapture(_deviceIndex, VideoCaptureAPIs.DSHOW);

        if (!_capture.IsOpened())
        {
            Console.WriteLine($"[CaptureCard] Session {_sessionId}: Failed to open device {_deviceIndex}");
            throw new InvalidOperationException($"Cannot open capture device {_deviceIndex}");
        }

        // Configure resolution and FPS
        _capture.Set(VideoCaptureProperties.FrameWidth, _captureWidth);
        _capture.Set(VideoCaptureProperties.FrameHeight, _captureHeight);
        _capture.Set(VideoCaptureProperties.Fps, TargetFps);

        var actualW = _capture.Get(VideoCaptureProperties.FrameWidth);
        var actualH = _capture.Get(VideoCaptureProperties.FrameHeight);
        var actualFps = _capture.Get(VideoCaptureProperties.Fps);

        Console.WriteLine($"[CaptureCard] Session {_sessionId}: Opened {_deviceName} at {actualW}x{actualH} @ {actualFps}fps");

        _cts = new CancellationTokenSource();
        _isCapturing = true;
        _captureLoop = Task.Factory.StartNew(
            () => CaptureLoop(_cts.Token),
            _cts.Token,
            TaskCreationOptions.LongRunning,
            TaskScheduler.Default);

        return Task.CompletedTask;
    }

    public Task StopAsync()
    {
        _isCapturing = false;
        _cts?.Cancel();

        try { _captureLoop?.Wait(TimeSpan.FromSeconds(2)); } catch { }

        _capture?.Release();
        _capture?.Dispose();
        _capture = null;
        _cts?.Dispose();
        _cts = null;

        Console.WriteLine($"[CaptureCard] Session {_sessionId}: Stopped.");
        return Task.CompletedTask;
    }

    // ---------------------------------------------------------------------------
    // Capture Loop
    // ---------------------------------------------------------------------------

    private void CaptureLoop(CancellationToken ct)
    {
        var frame = new Mat();
        var sw = Stopwatch.StartNew();
        int frameCount = 0;
        double frameInterval = 1000.0 / TargetFps;

        Console.WriteLine($"[CaptureCard] Session {_sessionId}: Capture loop started ({TargetFps}fps)");

        while (!ct.IsCancellationRequested)
        {
            var frameStart = sw.ElapsedMilliseconds;

            try
            {
                if (_capture is null || !_capture.IsOpened()) break;

                if (_capture.Read(frame) && !frame.Empty())
                {
                    // Encode to JPEG
                    var jpegParams = new ImageEncodingParam(ImwriteFlags.JpegQuality, 70);
                    Cv2.ImEncode(".jpg", frame, out var jpegBytes, jpegParams);

                    FrameReady?.Invoke(_sessionId, jpegBytes);

                    frameCount++;
                    if (frameCount <= 3 || frameCount % 100 == 0)
                        Console.WriteLine($"[CaptureCard] Session {_sessionId}: Frame #{frameCount} ({jpegBytes.Length} bytes)");
                }
            }
            catch (Exception ex)
            {
                if (!ct.IsCancellationRequested)
                    Console.WriteLine($"[CaptureCard] Session {_sessionId}: Capture error: {ex.Message}");
            }

            // FPS cap
            var elapsed = sw.ElapsedMilliseconds - frameStart;
            var sleepMs = (int)(frameInterval - elapsed);
            if (sleepMs > 0)
                Thread.Sleep(sleepMs);
        }

        frame.Dispose();
        Console.WriteLine($"[CaptureCard] Session {_sessionId}: Capture loop ended ({frameCount} frames total)");
    }

    public void Dispose()
    {
        StopAsync().Wait(TimeSpan.FromSeconds(2));
    }
}
