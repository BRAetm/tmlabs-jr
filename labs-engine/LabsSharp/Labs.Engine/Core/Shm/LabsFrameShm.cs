using System;
using System.Collections.Concurrent;
using System.IO.MemoryMappedFiles;
using System.Threading;

namespace Labs.Engine.Core.Shm;

/// <summary>
/// Per-session raw BGRA frame bus. One <see cref="MemoryMappedFile"/> per session
/// ("Labs_&lt;pid&gt;_Frame_&lt;sessionId&gt;") sized generously for up to 1920×1080×4;
/// the writer stamps a 64-byte header and the pixel payload, then sets a named
/// event so Python readers wake and snapshot a coherent frame.
///
/// Header layout (64 bytes, little-endian):
///   [ 0.. 3] magic 0x4D52464C ("FRML")
///   [ 4.. 7] version          (=1)
///   [ 8..11] writer_pid
///   [12..15] sequence         (bumped AFTER payload write)
///   [16..19] payload_size     (w * h * bpp)
///   [20..23] width
///   [24..27] height
///   [28..31] stride           (bytes per row)
///   [32..35] format           (0=BGRA, 1=BGR, 2=NV12)
///   [36..39] session_id
///   [40..47] timestamp_ms     (int64, Environment.TickCount64)
///   [48..63] reserved
///
/// Zero-copy from Python: mmap the block, wrap payload as a numpy view.
/// </summary>
public static class LabsFrameShm
{
    public const uint Magic = 0x4D52464C; // 'F''R''M''L' little-endian
    public const uint Version = 1;
    public const int HeaderSize = 64;

    public const int FormatBgra = 0;
    public const int FormatBgr  = 1;
    public const int FormatNv12 = 2;

    internal const int OffMagic       = 0;
    internal const int OffVersion     = 4;
    internal const int OffWriterPid   = 8;
    internal const int OffSequence    = 12;
    internal const int OffPayloadSize = 16;
    internal const int OffWidth       = 20;
    internal const int OffHeight      = 24;
    internal const int OffStride      = 28;
    internal const int OffFormat      = 32;
    internal const int OffSessionId   = 36;
    internal const int OffTimestampMs = 40;

    // Cap: 1920×1080×4 + header + headroom.
    public const int MaxPayload = 1920 * 1080 * 4;
    public const int BlockSize  = HeaderSize + MaxPayload;

    public static string BlockName(int pid, int sessionId) => $"Labs_{pid}_Frame_{sessionId}";
    public static string EventName(int pid, int sessionId) => $@"Global\Labs_{pid}_Frame_{sessionId}_Written";
}

/// <summary>
/// One writer per session. Kept alive by <see cref="LabsFrameShmWriter"/> below;
/// disposed when the session ends.
/// </summary>
public sealed class LabsFrameShmSession : IDisposable
{
    private readonly int _pid;
    private readonly int _sessionId;
    private readonly MemoryMappedFile _mmf;
    private readonly MemoryMappedViewAccessor _view;
    private readonly EventWaitHandle _updated;
    private uint _sequence;
    private bool _disposed;

    public LabsFrameShmSession(int sessionId)
    {
        _pid = Environment.ProcessId;
        _sessionId = sessionId;

        _mmf = MemoryMappedFile.CreateOrOpen(
            LabsFrameShm.BlockName(_pid, _sessionId),
            LabsFrameShm.BlockSize,
            MemoryMappedFileAccess.ReadWrite);
        _view = _mmf.CreateViewAccessor(0, LabsFrameShm.BlockSize, MemoryMappedFileAccess.ReadWrite);
        _updated = new EventWaitHandle(false, EventResetMode.AutoReset,
            LabsFrameShm.EventName(_pid, _sessionId));

        _view.Write(LabsFrameShm.OffMagic,     LabsFrameShm.Magic);
        _view.Write(LabsFrameShm.OffVersion,   LabsFrameShm.Version);
        _view.Write(LabsFrameShm.OffWriterPid, (uint)_pid);
        _view.Write(LabsFrameShm.OffSessionId, _sessionId);

        Console.WriteLine($"[LabsFrameShm] session {_sessionId} opened block '{LabsFrameShm.BlockName(_pid, _sessionId)}'");
    }

    public void Publish(int width, int height, int stride, int format, ReadOnlySpan<byte> pixels)
    {
        if (_disposed) return;
        int payloadSize = pixels.Length;
        if (payloadSize <= 0 || payloadSize > LabsFrameShm.MaxPayload) return;

        // Write payload first.
        unsafe
        {
            byte* p = null;
            try
            {
                _view.SafeMemoryMappedViewHandle.AcquirePointer(ref p);
                fixed (byte* src = pixels)
                {
                    Buffer.MemoryCopy(src, p + LabsFrameShm.HeaderSize, LabsFrameShm.MaxPayload, payloadSize);
                }
            }
            finally
            {
                if (p != null) _view.SafeMemoryMappedViewHandle.ReleasePointer();
            }
        }

        // Header (width/height/stride/format/timestamp/payload_size), then bump sequence.
        _view.Write(LabsFrameShm.OffPayloadSize, (uint)payloadSize);
        _view.Write(LabsFrameShm.OffWidth,       (uint)width);
        _view.Write(LabsFrameShm.OffHeight,      (uint)height);
        _view.Write(LabsFrameShm.OffStride,      (uint)stride);
        _view.Write(LabsFrameShm.OffFormat,      (uint)format);
        _view.Write(LabsFrameShm.OffTimestampMs, Environment.TickCount64);

        unchecked { _sequence++; }
        _view.Write(LabsFrameShm.OffSequence, _sequence);

        try { _updated.Set(); } catch { }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        try { _view.Dispose(); } catch { }
        try { _mmf.Dispose(); } catch { }
        try { _updated.Dispose(); } catch { }
    }
}

/// <summary>
/// Multiplexes <see cref="LabsFrameShmSession"/>s by session id. One writer instance
/// for the whole engine; lazy-opens a block the first time a session id is seen.
/// </summary>
public sealed class LabsFrameShmWriter : IDisposable
{
    private readonly ConcurrentDictionary<int, LabsFrameShmSession> _sessions = new();
    private bool _disposed;

    public void Publish(int sessionId, int width, int height, int stride, int format, ReadOnlySpan<byte> pixels)
    {
        if (_disposed) return;
        var session = _sessions.GetOrAdd(sessionId, id => new LabsFrameShmSession(id));
        session.Publish(width, height, stride, format, pixels);
    }

    public void Close(int sessionId)
    {
        if (_sessions.TryRemove(sessionId, out var s)) s.Dispose();
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        foreach (var kv in _sessions) { try { kv.Value.Dispose(); } catch { } }
        _sessions.Clear();
    }
}
