using System;
using System.IO.MemoryMappedFiles;
using System.Threading;

namespace Labs.Engine.Core;

/// <summary>
/// Writes raw BGR frame pixels to a shared memory-mapped file that Python reads directly.
/// No JPEG encoding, no ZMQ — just a fast memcpy. Achieves 60+ FPS.
///
/// Memory layout (header = 16 bytes):
///   [0..3]   magic "FRMX" (4 bytes)
///   [4..7]   width  (int32 LE)
///   [8..11]  height (int32 LE)
///   [12..15] frame counter (int32 LE, incremented each write)
///   [16..]   raw BGR pixel data (width * height * 3 bytes)
/// </summary>
public sealed class SharedFrameBridge : IDisposable
{
    public const string DefaultMapName = "TMLabs_FrameBridge";
    private const int HeaderSize = 16;
    private const int MaxWidth = 1920;
    private const int MaxHeight = 1080;
    private const int MaxDataSize = MaxWidth * MaxHeight * 3 + HeaderSize;

    private MemoryMappedFile? _mmf;
    private MemoryMappedViewAccessor? _accessor;
    private int _frameCounter;
    private bool _disposed;

    public bool IsActive => _accessor is not null;

    public void Start()
    {
        _mmf = MemoryMappedFile.CreateOrOpen(DefaultMapName, MaxDataSize, MemoryMappedFileAccess.ReadWrite);
        _accessor = _mmf.CreateViewAccessor(0, MaxDataSize, MemoryMappedFileAccess.ReadWrite);

        // Write magic header
        _accessor.Write(0, (byte)'F');
        _accessor.Write(1, (byte)'R');
        _accessor.Write(2, (byte)'M');
        _accessor.Write(3, (byte)'X');
        _accessor.Write(4, 0); // width
        _accessor.Write(8, 0); // height
        _accessor.Write(12, 0); // counter

        Console.WriteLine($"[SharedFrameBridge] Shared memory '{DefaultMapName}' created ({MaxDataSize / 1024 / 1024}MB)");
    }

    /// <summary>Writes a BitmapSource frame to shared memory as raw BGR pixels.</summary>
    public void WriteFrame(System.Windows.Media.Imaging.BitmapSource frame)
    {
        if (_accessor is null || _disposed) return;

        try
        {
            int w = frame.PixelWidth;
            int h = frame.PixelHeight;

            // Downscale if too large (keep it fast)
            if (w > 960)
            {
                double scale = 960.0 / w;
                int newW = (int)(w * scale);
                int newH = (int)(h * scale);
                var scaled = new System.Windows.Media.Imaging.TransformedBitmap(
                    frame, new System.Windows.Media.ScaleTransform(scale, scale));
                scaled.Freeze();
                frame = scaled;
                w = frame.PixelWidth;
                h = frame.PixelHeight;
            }

            // Convert to BGR24
            System.Windows.Media.Imaging.BitmapSource bgr;
            if (frame.Format != System.Windows.Media.PixelFormats.Bgr24)
            {
                bgr = new System.Windows.Media.Imaging.FormatConvertedBitmap(
                    frame, System.Windows.Media.PixelFormats.Bgr24, null, 0);
                ((System.Windows.Media.Imaging.FormatConvertedBitmap)bgr).Freeze();
            }
            else
            {
                bgr = frame;
            }

            int stride = w * 3;
            int dataSize = stride * h;

            if (dataSize + HeaderSize > MaxDataSize) return; // too large

            // Write header
            _accessor.Write(4, w);
            _accessor.Write(8, h);

            // Write pixel data
            var pixels = new byte[dataSize];
            bgr.CopyPixels(pixels, stride, 0);
            _accessor.WriteArray(HeaderSize, pixels, 0, dataSize);

            // Increment frame counter last (signals to reader that a new frame is ready)
            _frameCounter++;
            _accessor.Write(12, _frameCounter);
        }
        catch (Exception ex)
        {
            if (_frameCounter <= 3)
                Console.WriteLine($"[SharedFrameBridge] Write error: {ex.Message}");
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _accessor?.Dispose();
        _mmf?.Dispose();
        Console.WriteLine("[SharedFrameBridge] Disposed.");
    }
}
