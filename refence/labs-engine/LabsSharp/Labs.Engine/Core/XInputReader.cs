using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace Labs.Engine.Core;

/// <summary>
/// Polls the first connected XInput controller at ~60Hz and raises
/// <see cref="StateChanged"/> whenever the pad report changes. Used as an
/// input source for the on-screen monitor and (optionally) as a second
/// producer that the engine can fan out to sinks.
/// </summary>
public sealed class XInputReader : IDisposable
{
    private const int XINPUT_GAMEPAD_DPAD_UP        = 0x0001;
    private const int XINPUT_GAMEPAD_DPAD_DOWN      = 0x0002;
    private const int XINPUT_GAMEPAD_DPAD_LEFT      = 0x0004;
    private const int XINPUT_GAMEPAD_DPAD_RIGHT     = 0x0008;
    private const int XINPUT_GAMEPAD_START          = 0x0010;
    private const int XINPUT_GAMEPAD_BACK           = 0x0020;
    private const int XINPUT_GAMEPAD_LEFT_THUMB     = 0x0040;
    private const int XINPUT_GAMEPAD_RIGHT_THUMB    = 0x0080;
    private const int XINPUT_GAMEPAD_LEFT_SHOULDER  = 0x0100;
    private const int XINPUT_GAMEPAD_RIGHT_SHOULDER = 0x0200;
    private const int XINPUT_GAMEPAD_A              = 0x1000;
    private const int XINPUT_GAMEPAD_B              = 0x2000;
    private const int XINPUT_GAMEPAD_X              = 0x4000;
    private const int XINPUT_GAMEPAD_Y              = 0x8000;

    [StructLayout(LayoutKind.Sequential)]
    private struct XINPUT_GAMEPAD
    {
        public ushort wButtons;
        public byte bLeftTrigger;
        public byte bRightTrigger;
        public short sThumbLX;
        public short sThumbLY;
        public short sThumbRX;
        public short sThumbRY;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct XINPUT_STATE
    {
        public uint dwPacketNumber;
        public XINPUT_GAMEPAD Gamepad;
    }

    [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")]
    private static extern int XInputGetState14(int dwUserIndex, out XINPUT_STATE pState);

    [DllImport("xinput9_1_0.dll", EntryPoint = "XInputGetState")]
    private static extern int XInputGetState910(int dwUserIndex, out XINPUT_STATE pState);

    private static int GetState(int idx, out XINPUT_STATE s)
    {
        try { return XInputGetState14(idx, out s); }
        catch (DllNotFoundException) { return XInputGetState910(idx, out s); }
    }

    private readonly CancellationTokenSource _cts = new();
    private readonly Task _loop;
    private uint _lastPacket;
    private int _lastConnectedIdx = -1;
    private bool _disposed;

    /// <summary>Fired whenever any connected pad's packet number changes.</summary>
    public event Action<GamepadState>? StateChanged;

    /// <summary>Fired when a controller connects or disconnects (bool = connected).</summary>
    public event Action<bool>? ConnectionChanged;

    public bool IsConnected => _lastConnectedIdx >= 0;

    public XInputReader()
    {
        _loop = Task.Run(() => PollLoop(_cts.Token));
    }

    private void PollLoop(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            int found = -1;
            XINPUT_STATE st = default;
            for (int i = 0; i < 4; i++)
            {
                if (GetState(i, out st) == 0) { found = i; break; }
            }

            if (found != _lastConnectedIdx)
            {
                _lastConnectedIdx = found;
                try { ConnectionChanged?.Invoke(found >= 0); } catch { }
                _lastPacket = 0;
            }

            if (found >= 0 && st.dwPacketNumber != _lastPacket)
            {
                _lastPacket = st.dwPacketNumber;
                try { StateChanged?.Invoke(ToGamepadState(st.Gamepad)); } catch { }
            }

            try { Thread.Sleep(16); } catch { }
        }
    }

    private static GamepadState ToGamepadState(XINPUT_GAMEPAD g)
    {
        var s = new GamepadState();

        s.Axes[0] = Norm(g.sThumbLX);
        s.Axes[1] = -Norm(g.sThumbLY); // XInput Y is up-positive; Web Gamepad is down-positive
        s.Axes[2] = Norm(g.sThumbRX);
        s.Axes[3] = -Norm(g.sThumbRY);

        s.Buttons[0]  = (g.wButtons & XINPUT_GAMEPAD_A) != 0;
        s.Buttons[1]  = (g.wButtons & XINPUT_GAMEPAD_B) != 0;
        s.Buttons[2]  = (g.wButtons & XINPUT_GAMEPAD_X) != 0;
        s.Buttons[3]  = (g.wButtons & XINPUT_GAMEPAD_Y) != 0;
        s.Buttons[4]  = (g.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
        s.Buttons[5]  = (g.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
        s.Buttons[6]  = g.bLeftTrigger  > 32;
        s.Buttons[7]  = g.bRightTrigger > 32;
        s.Buttons[8]  = (g.wButtons & XINPUT_GAMEPAD_BACK) != 0;
        s.Buttons[9]  = (g.wButtons & XINPUT_GAMEPAD_START) != 0;
        s.Buttons[10] = (g.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        s.Buttons[11] = (g.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
        s.Buttons[12] = (g.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
        s.Buttons[13] = (g.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
        s.Buttons[14] = (g.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
        s.Buttons[15] = (g.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
        s.Buttons[16] = false; // XInput doesn't expose Guide via public API
        return s;
    }

    private static float Norm(short v)
    {
        // 8000-unit dead zone (per XInput docs) then scale to [-1,1].
        const int dz = 8000;
        if (Math.Abs((int)v) < dz) return 0f;
        float f = v < 0 ? (v + dz) / (32768f - dz) : (v - dz) / (32767f - dz);
        return Math.Clamp(f, -1f, 1f);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        try { _cts.Cancel(); } catch { }
        try { _loop.Wait(500); } catch { }
        try { _cts.Dispose(); } catch { }
    }
}
