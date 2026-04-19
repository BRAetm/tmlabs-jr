using System;
using System.Runtime.InteropServices;
using System.Threading;
using Labs.Native;

namespace Labs.Engine.Input;

/// <summary>
/// Minimal keyboard-to-DualSense mapping, polled at ~120 Hz and forwarded via
/// labs_session_set_controller_state. Extend or swap in XInput/SDL2 for real
/// gamepad support. Call <see cref="SetButton"/> and <see cref="SetStick"/>
/// from key/axis handlers; Tick dispatches.
/// </summary>
public sealed class ControllerInput : IDisposable
{
    private readonly IntPtr _session;
    private LabsNative.LabsControllerState _state;
    private readonly Timer _timer;
    private readonly object _gate = new();

    public ControllerInput(IntPtr session)
    {
        _session = session;
        LabsNative.labs_controller_state_set_idle(ref _state);
        _timer = new Timer(_ => Flush(), null, TimeSpan.FromMilliseconds(10), TimeSpan.FromMilliseconds(8));
    }

    public void SetButton(LabsNative.LabsControllerButton btn, bool down)
    {
        lock (_gate)
        {
            if (down) _state.buttons |= (uint)btn;
            else _state.buttons &= ~(uint)btn;
        }
    }

    public void SetLeftStick(short x, short y)  { lock (_gate) { _state.left_x  = x; _state.left_y  = y; } }
    public void SetRightStick(short x, short y) { lock (_gate) { _state.right_x = x; _state.right_y = y; } }
    public void SetTriggers(byte l2, byte r2)   { lock (_gate) { _state.l2_state = l2; _state.r2_state = r2; } }

    private void Flush()
    {
        LabsNative.LabsControllerState snapshot;
        lock (_gate) { snapshot = _state; }
        LabsNative.labs_session_set_controller_state(_session, ref snapshot);
    }

    public void Dispose() => _timer.Dispose();
}
