using System;
using System.IO.MemoryMappedFiles;
using System.Threading;
using System.Threading.Tasks;

namespace Labs.Engine.Core.Shm;

/// <summary>
/// Shared-memory transport for <see cref="GamepadState"/>. One writer (external
/// process — labs-labeler CV script) and one reader (labs-engine, which forwards
/// to the active <see cref="IGamepadSink"/>).
///
/// Replaces <c>GamepadHttpBridge</c>: per-frame HTTP POSTs cost ~1 ms round-trip
/// and drop under load. Shared memory is zero-copy — sub-microsecond. Writers and
/// readers can live in different processes as long as they agree on the PID used
/// in the block name.
///
/// Payload layout (64 bytes after the 32-byte header):
///   [ 0..15 ] axes[4]   (float32)
///   [16..32 ] buttons[17] (uint8, 0 or 1)
///   [33..35 ] padding
///   [36..39 ] session_id (int32)
///   [40..63 ] reserved
/// </summary>
public static class LabsGamepadShm
{
    public const string Suffix = "Gamepad";
    public const int PayloadSize = 64;
    public const int BlockSize = LabsShmHeader.Size + PayloadSize;

    internal const int OffsetAxes      = LabsShmHeader.Size + 0;   // 4 × float
    internal const int OffsetButtons   = LabsShmHeader.Size + 16;  // 17 × byte
    internal const int OffsetSessionId = LabsShmHeader.Size + 36;  // int32
}

/// <summary>
/// Writes <see cref="GamepadState"/> to the Labs gamepad block. Instantiate once
/// per process; call <see cref="Publish"/> every frame you want applied.
/// </summary>
public sealed class LabsGamepadShmWriter : IDisposable
{
    private readonly MemoryMappedFile _mmf;
    private readonly MemoryMappedViewAccessor _accessor;
    private readonly EventWaitHandle _written;
    private uint _sequence;
    private bool _disposed;

    public int WriterPid { get; }

    /// <param name="writerPid">PID whose name the block uses. Defaults to the current process.</param>
    public LabsGamepadShmWriter(int? writerPid = null)
    {
        WriterPid = writerPid ?? Environment.ProcessId;
        _mmf = MemoryMappedFile.CreateOrOpen(
            LabsShmHeader.BlockName(LabsGamepadShm.Suffix, WriterPid),
            LabsGamepadShm.BlockSize,
            MemoryMappedFileAccess.ReadWrite);
        _accessor = _mmf.CreateViewAccessor(0, LabsGamepadShm.BlockSize, MemoryMappedFileAccess.ReadWrite);
        LabsShmHeader.Initialize(_accessor, LabsGamepadShm.PayloadSize);
        _written = new EventWaitHandle(false, EventResetMode.AutoReset,
            LabsShmHeader.WrittenEventName(LabsGamepadShm.Suffix, WriterPid));
    }

    /// <summary>Publish one gamepad frame. Increments the sequence and signals readers.</summary>
    public void Publish(int sessionId, GamepadState state)
    {
        if (_disposed || state is null) return;

        // Write payload first …
        for (int i = 0; i < 4; i++)
        {
            var v = i < state.Axes.Length ? state.Axes[i] : 0f;
            _accessor.Write(LabsGamepadShm.OffsetAxes + i * 4, v);
        }
        for (int i = 0; i < 17; i++)
        {
            var b = i < state.Buttons.Length && state.Buttons[i];
            _accessor.Write(LabsGamepadShm.OffsetButtons + i, (byte)(b ? 1 : 0));
        }
        _accessor.Write(LabsGamepadShm.OffsetSessionId, sessionId);

        // … then bump the sequence so readers can snap a consistent frame …
        unchecked { _sequence++; }
        LabsShmHeader.PublishSequence(_accessor, _sequence);

        // … and wake the reader.
        try { _written.Set(); } catch { }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        try { _accessor.Dispose(); } catch { }
        try { _mmf.Dispose(); } catch { }
        try { _written.Dispose(); } catch { }
    }
}

/// <summary>
/// Polls the Labs gamepad block and hands every new frame to an <see cref="IGamepadSink"/>.
/// Use from the engine process as a drop-in replacement for <c>GamepadHttpBridge</c>.
/// Reader wakes on the Written event but falls back to 10 ms polling so startup-order
/// quirks (reader up before writer) don't deadlock.
/// </summary>
public sealed class LabsGamepadShmReader : IDisposable
{
    private readonly int _pid;
    private readonly CancellationTokenSource _cts = new();
    private IGamepadSink? _sink;
    private int _sessionId;
    private Task? _loop;
    private bool _disposed;

    /// <summary>True once the background polling loop is running.</summary>
    public bool IsRunning => _loop is { IsCompleted: false };

    /// <param name="writerPid">PID used in the block name. Defaults to current process
    /// (so writer + reader in the same process, e.g. engine hosting the script).</param>
    public LabsGamepadShmReader(int? writerPid = null)
    {
        _pid = writerPid ?? Environment.ProcessId;
    }

    public void SetActiveSink(IGamepadSink? sink, int sessionId)
    {
        _sink = sink;
        _sessionId = sessionId;
    }

    public void Start()
    {
        if (_loop is not null) return;
        _loop = Task.Run(() => Loop(_cts.Token));
        Console.WriteLine($"[LabsGamepadShmReader] reading from '{LabsShmHeader.BlockName(LabsGamepadShm.Suffix, _pid)}'");
    }

    private void Loop(CancellationToken ct)
    {
        MemoryMappedFile? mmf = null;
        MemoryMappedViewAccessor? a = null;
        EventWaitHandle? written = null;
        uint lastSeq = 0;

        while (!ct.IsCancellationRequested)
        {
            if (a is null)
            {
                try
                {
                    mmf = MemoryMappedFile.OpenExisting(
                        LabsShmHeader.BlockName(LabsGamepadShm.Suffix, _pid),
                        MemoryMappedFileRights.Read);
                    a = mmf.CreateViewAccessor(0, LabsGamepadShm.BlockSize, MemoryMappedFileAccess.Read);
                    EventWaitHandle.TryOpenExisting(
                        LabsShmHeader.WrittenEventName(LabsGamepadShm.Suffix, _pid), out written);
                }
                catch
                {
                    // Writer isn't up yet — back off briefly and retry.
                    ct.WaitHandle.WaitOne(100);
                    continue;
                }
            }

            // Wait for the writer's pulse, but never longer than 10 ms so we stay responsive
            // even if the event handle couldn't be opened (some sandbox configurations).
            if (written is not null) written.WaitOne(10);
            else ct.WaitHandle.WaitOne(10);

            if (!LabsShmHeader.IsValid(a)) continue;
            var seq = LabsShmHeader.ReadSequence(a);
            if (seq == lastSeq) continue;
            lastSeq = seq;

            var state = new GamepadState();
            for (int i = 0; i < 4; i++)
                state.Axes[i] = a.ReadSingle(LabsGamepadShm.OffsetAxes + i * 4);
            for (int i = 0; i < 17; i++)
                state.Buttons[i] = a.ReadByte(LabsGamepadShm.OffsetButtons + i) != 0;

            var sink = _sink;
            if (sink is null) continue;

            try { sink.SendInput(_sessionId, state); }
            catch (Exception ex)
            {
                Console.WriteLine($"[LabsGamepadShmReader] sink error: {ex.Message}");
            }
        }

        try { a?.Dispose(); } catch { }
        try { mmf?.Dispose(); } catch { }
        try { written?.Dispose(); } catch { }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        try { _cts.Cancel(); } catch { }
        try { _loop?.Wait(500); } catch { }
        try { _cts.Dispose(); } catch { }
    }
}
