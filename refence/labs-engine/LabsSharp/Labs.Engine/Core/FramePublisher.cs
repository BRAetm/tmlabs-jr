using System;
using System.IO;
using System.Windows.Media.Imaging;
using Labs.Engine.Core.Shm;
using NetMQ;
using NetMQ.Sockets;

namespace Labs.Engine.Core;

/// <summary>
/// Publishes captured frames to Python scripts. The hot path is the per-session
/// shared-memory block (<see cref="LabsFrameShmWriter"/>): a raw BGRA dump with a
/// 64-byte header, wrapped as a numpy view on the Python side for zero-copy CV.
/// The legacy ZMQ PUB socket stays available as a fallback for consumers that
/// haven't been migrated to SHM yet.
/// </summary>
public sealed class FramePublisher : IDisposable
{
    /// <summary>Port for the frame PUB socket (ZMQ fallback path).</summary>
    public const int PubPort = 5580;

    private readonly object _lock = new();
    private readonly LabsFrameShmWriter _shm = new();
    private PublisherSocket? _pub;
    private bool _disposed;

    /// <summary>Binds the PUB socket on the configured port.</summary>
    public void Start()
    {
        try
        {
            _pub = new PublisherSocket();
            _pub.Options.SendHighWatermark = 1;
            _pub.Bind($"tcp://127.0.0.1:{PubPort}");
            Console.WriteLine($"[FramePublisher] PUB socket bound on port {PubPort} (SNDHWM=1).");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[FramePublisher] ZMQ bind failed ({ex.Message}); continuing with SHM only.");
            try { _pub?.Dispose(); } catch { }
            _pub = null;
        }
    }

    /// <summary>Publishes a frame for the given session. Topic is "frame_{sessionId}".</summary>
    public void PublishFrame(int sessionId, BitmapSource frame)
    {
        if (_pub is null || _disposed) return;

        try
        {
            var bytes = BitmapSourceToJpeg(frame);
            lock (_lock) { _pub.SendMoreFrame($"frame_{sessionId}").SendFrame(bytes); }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[FramePublisher] Session {sessionId}: publish failed — {ex.Message}");
        }
    }

    private int _rawFrameCount;

    /// <summary>Publishes raw JPEG bytes for the given session — skips BitmapSource encoding.</summary>
    public void PublishRawFrame(int sessionId, byte[] jpegBytes)
    {
        if (_pub is null || _disposed) return;

        try
        {
            lock (_lock) { _pub.SendMoreFrame($"frame_{sessionId}").SendFrame(jpegBytes); }
            _rawFrameCount++;
            if (_rawFrameCount % 50 == 1)
                Console.WriteLine($"[FramePublisher] Session {sessionId}: published frame #{_rawFrameCount} ({jpegBytes.Length} bytes)");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[FramePublisher] Session {sessionId}: raw publish failed — {ex.Message}");
        }
    }

    /// <summary>
    /// Publishes a raw BGRA frame to the per-session SHM block — zero compression,
    /// no JPEG round-trip. Preferred over <see cref="PublishFrame"/> / <see cref="PublishRawFrame"/>
    /// when the source already has decoded pixel data in hand.
    /// </summary>
    public void PublishBgra(int sessionId, int width, int height, int stride, ReadOnlySpan<byte> bgra)
    {
        if (_disposed) return;
        try { _shm.Publish(sessionId, width, height, stride, LabsFrameShm.FormatBgra, bgra); }
        catch (Exception ex)
        {
            Console.WriteLine($"[FramePublisher] Session {sessionId}: shm publish failed — {ex.Message}");
        }
    }

    /// <summary>Closes the SHM block for a session when it ends.</summary>
    public void CloseSession(int sessionId) => _shm.Close(sessionId);

    /// <summary>Encodes a frozen BitmapSource as JPEG bytes for efficient ZMQ transfer.</summary>
    private static byte[] BitmapSourceToJpeg(BitmapSource bmp)
    {
        var encoder = new JpegBitmapEncoder { QualityLevel = 85 };
        encoder.Frames.Add(BitmapFrame.Create(bmp));
        using var ms = new MemoryStream();
        encoder.Save(ms);
        return ms.ToArray();
    }

    /// <summary>Closes the PUB socket and releases resources.</summary>
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _pub?.Dispose();
        _pub = null;
        try { _shm.Dispose(); } catch { }
        Console.WriteLine("[FramePublisher] Disposed.");
    }
}
