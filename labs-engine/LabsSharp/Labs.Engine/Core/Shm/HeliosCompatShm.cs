using System;
using System.IO.MemoryMappedFiles;
using System.Threading;
using System.Threading.Tasks;

namespace Labs.Engine.Core.Shm;

/// <summary>
/// Helios-convention shared-memory blocks consumed by the prebuilt GPC3 VM
/// (<c>gpc3vm-run.exe --helios-pid &lt;pid&gt;</c>). The VM is a compiled binary we
/// don't control — it opens blocks named <c>Helios_&lt;pid&gt;_ControllerInput</c>
/// and <c>Helios_&lt;pid&gt;_ControllerOutput</c>, and signals
/// <c>Global\Helios_&lt;pid&gt;_ControllerOutput_Updated</c> when it has produced
/// a modified controller state. Our engine wraps Labs-branded code around these
/// legacy names so a GPC3 script can read live gamepad state and write modified
/// output back into the ViGEm sink.
///
/// Block layout (37 bytes, matches Helios <c>SharedMemoryControllerState</c>):
///   [ 0..31] data[32]        — DS5-style controller report (first 32 bytes)
///   [32..35] timestamp_ms    — writer's monotonic timestamp
///   [36]     source          — 0=DS5 1=DS4 2=XInput 3=ViGEm
///
/// DS5 report slice used here (offsets within data[]):
///   0: report_id (0x01)
///   1: left_stick_x  (0..255, centered 128)
///   2: left_stick_y
///   3: right_stick_x
///   4: right_stick_y
///   5: l2_trigger    (0..255)
///   6: r2_trigger
///   7: seq_num       (rolling counter)
///   8: buttons[0]    — low nibble=dpad (0=N..7=NW, 8=released), high nibble=face (□△○✕)
///   9: buttons[1]    — L1 R1 L2c R2c Create Options L3 R3
///  10: buttons[2]    — PS TouchClick Mute
///  11..31: zero (gyro/accel/touch/timestamp reserved)
/// </summary>
public static class HeliosCompatShm
{
    public const int StateSize = 37;
    public const int DataBytes = 32;

    internal const int OffsetData        = 0;
    internal const int OffsetTimestampMs = 32;
    internal const int OffsetSource      = 36;

    public const byte SourceDs5   = 0;
    public const byte SourceViGEm = 3;

    public static string BlockName(string suffix, int pid)
        => $"Helios_{pid}_{suffix}";

    public static string EventName(string suffix, int pid)
        => $@"Global\Helios_{pid}_{suffix}_Updated";

    /// <summary>Fills a 32-byte DS5-style report from a Web Gamepad state.</summary>
    public static void EncodeDs5(GamepadState state, Span<byte> report, byte seq)
    {
        report.Clear();
        report[0] = 0x01; // report_id

        report[1] = AxisToByte(state.Axes[0]);
        report[2] = AxisToByte(state.Axes[1]);
        report[3] = AxisToByte(state.Axes[2]);
        report[4] = AxisToByte(state.Axes[3]);
        report[5] = state.Buttons[6] ? (byte)255 : (byte)0; // L2
        report[6] = state.Buttons[7] ? (byte)255 : (byte)0; // R2
        report[7] = seq;

        // buttons[0]: dpad low-nibble + face high-nibble
        byte dpad = DpadFromWebGamepad(state);
        byte face = 0;
        if (state.Buttons[2]) face |= 0x10; // Square
        if (state.Buttons[0]) face |= 0x20; // Cross (A)
        if (state.Buttons[1]) face |= 0x40; // Circle (B)
        if (state.Buttons[3]) face |= 0x80; // Triangle (Y)
        report[8] = (byte)(dpad | face);

        // buttons[1]: L1 R1 L2click R2click Create Options L3 R3
        byte b1 = 0;
        if (state.Buttons[4]) b1 |= 0x01; // L1
        if (state.Buttons[5]) b1 |= 0x02; // R1
        if (state.Buttons[6]) b1 |= 0x04; // L2 click
        if (state.Buttons[7]) b1 |= 0x08; // R2 click
        if (state.Buttons[8]) b1 |= 0x10; // Create (Back)
        if (state.Buttons[9]) b1 |= 0x20; // Options (Start)
        if (state.Buttons[10]) b1 |= 0x40; // L3
        if (state.Buttons[11]) b1 |= 0x80; // R3
        report[9] = b1;

        // buttons[2]: PS TouchClick Mute
        byte b2 = 0;
        if (state.Buttons[16]) b2 |= 0x01; // PS / Guide
        report[10] = b2;
    }

    /// <summary>Parses a 32-byte DS5-style report back into a Web Gamepad state.</summary>
    public static GamepadState DecodeDs5(ReadOnlySpan<byte> report)
    {
        var s = new GamepadState();
        s.Axes[0] = ByteToAxis(report[1]);
        s.Axes[1] = ByteToAxis(report[2]);
        s.Axes[2] = ByteToAxis(report[3]);
        s.Axes[3] = ByteToAxis(report[4]);

        byte b0 = report[8];
        byte dpad = (byte)(b0 & 0x0F);
        // dpad: 0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW 8=released
        s.Buttons[12] = dpad is 0 or 1 or 7; // Up
        s.Buttons[13] = dpad is 3 or 4 or 5; // Down
        s.Buttons[14] = dpad is 5 or 6 or 7; // Left
        s.Buttons[15] = dpad is 1 or 2 or 3; // Right
        s.Buttons[2] = (b0 & 0x10) != 0; // Square → X
        s.Buttons[0] = (b0 & 0x20) != 0; // Cross → A
        s.Buttons[1] = (b0 & 0x40) != 0; // Circle → B
        s.Buttons[3] = (b0 & 0x80) != 0; // Triangle → Y

        byte b1 = report[9];
        s.Buttons[4]  = (b1 & 0x01) != 0;
        s.Buttons[5]  = (b1 & 0x02) != 0;
        s.Buttons[6]  = (b1 & 0x04) != 0 || report[5] > 32;
        s.Buttons[7]  = (b1 & 0x08) != 0 || report[6] > 32;
        s.Buttons[8]  = (b1 & 0x10) != 0;
        s.Buttons[9]  = (b1 & 0x20) != 0;
        s.Buttons[10] = (b1 & 0x40) != 0;
        s.Buttons[11] = (b1 & 0x80) != 0;

        s.Buttons[16] = (report[10] & 0x01) != 0;
        return s;
    }

    private static byte AxisToByte(float v)
    {
        var clamped = Math.Clamp(v, -1f, 1f);
        var mapped = (int)Math.Round(128 + clamped * 127f);
        return (byte)Math.Clamp(mapped, 0, 255);
    }

    private static float ByteToAxis(byte b)
        => Math.Clamp((b - 128) / 127f, -1f, 1f);

    private static byte DpadFromWebGamepad(GamepadState s)
    {
        bool up = s.Buttons[12], down = s.Buttons[13], left = s.Buttons[14], right = s.Buttons[15];
        if (up && right) return 1;
        if (right && down) return 3;
        if (down && left) return 5;
        if (left && up) return 7;
        if (up) return 0;
        if (right) return 2;
        if (down) return 4;
        if (left) return 6;
        return 8;
    }
}

/// <summary>
/// Publishes the live controller state into <c>Helios_&lt;pid&gt;_ControllerInput</c>
/// and watches <c>Helios_&lt;pid&gt;_ControllerOutput</c> for modified state from a
/// GPC3 script. Raises <see cref="OutputReceived"/> when the VM writes back so the
/// engine can forward to its active sink (ViGEm / Titan).
/// </summary>
public sealed class HeliosCompatShmBridge : IDisposable
{
    private readonly int _pid;
    private readonly MemoryMappedFile _inputMmf;
    private readonly MemoryMappedViewAccessor _inputAccessor;
    private readonly EventWaitHandle _inputUpdated;

    private readonly MemoryMappedFile _outputMmf;
    private readonly MemoryMappedViewAccessor _outputAccessor;
    private readonly EventWaitHandle _outputUpdated;

    private readonly CancellationTokenSource _cts = new();
    private readonly Task _outputLoop;
    private byte _seq;
    private bool _disposed;

    /// <summary>Fired on the background thread when the GPC3 VM writes a modified controller state.</summary>
    public event Action<GamepadState>? OutputReceived;

    public int HostPid => _pid;

    public HeliosCompatShmBridge()
    {
        _pid = Environment.ProcessId;

        _inputMmf = MemoryMappedFile.CreateOrOpen(
            HeliosCompatShm.BlockName("ControllerInput", _pid),
            HeliosCompatShm.StateSize, MemoryMappedFileAccess.ReadWrite);
        _inputAccessor = _inputMmf.CreateViewAccessor(0, HeliosCompatShm.StateSize, MemoryMappedFileAccess.ReadWrite);
        _inputUpdated = new EventWaitHandle(false, EventResetMode.AutoReset,
            HeliosCompatShm.EventName("ControllerInput", _pid));

        _outputMmf = MemoryMappedFile.CreateOrOpen(
            HeliosCompatShm.BlockName("ControllerOutput", _pid),
            HeliosCompatShm.StateSize, MemoryMappedFileAccess.ReadWrite);
        _outputAccessor = _outputMmf.CreateViewAccessor(0, HeliosCompatShm.StateSize, MemoryMappedFileAccess.ReadWrite);
        _outputUpdated = new EventWaitHandle(false, EventResetMode.AutoReset,
            HeliosCompatShm.EventName("ControllerOutput", _pid));

        _outputLoop = Task.Run(() => OutputLoop(_cts.Token));

        Console.WriteLine($"[HeliosCompatShm] serving blocks Helios_{_pid}_Controller{{Input,Output}}");
    }

    /// <summary>Publishes the current controller state for the GPC3 VM to read.</summary>
    public void PublishInput(GamepadState state)
    {
        if (_disposed || state is null) return;

        Span<byte> report = stackalloc byte[HeliosCompatShm.DataBytes];
        unchecked { _seq++; }
        HeliosCompatShm.EncodeDs5(state, report, _seq);

        byte[] buf = report.ToArray();
        _inputAccessor.WriteArray(HeliosCompatShm.OffsetData, buf, 0, buf.Length);
        _inputAccessor.Write(HeliosCompatShm.OffsetTimestampMs, (uint)Environment.TickCount);
        _inputAccessor.Write(HeliosCompatShm.OffsetSource, HeliosCompatShm.SourceViGEm);

        try { _inputUpdated.Set(); } catch { }
    }

    private void OutputLoop(CancellationToken ct)
    {
        byte[] buf = new byte[HeliosCompatShm.DataBytes];
        while (!ct.IsCancellationRequested)
        {
            if (!_outputUpdated.WaitOne(100)) continue;
            try
            {
                _outputAccessor.ReadArray(HeliosCompatShm.OffsetData, buf, 0, buf.Length);
                var state = HeliosCompatShm.DecodeDs5(buf);
                OutputReceived?.Invoke(state);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[HeliosCompatShm] output-read error: {ex.Message}");
            }
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        try { _cts.Cancel(); } catch { }
        try { _outputLoop.Wait(500); } catch { }
        try { _cts.Dispose(); } catch { }

        try { _inputAccessor.Dispose(); } catch { }
        try { _inputMmf.Dispose(); } catch { }
        try { _inputUpdated.Dispose(); } catch { }
        try { _outputAccessor.Dispose(); } catch { }
        try { _outputMmf.Dispose(); } catch { }
        try { _outputUpdated.Dispose(); } catch { }
    }
}
