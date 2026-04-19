using System;
using Labs.Engine.Core;
using Labs.Engine.Input;
using Labs.Native;

namespace Labs.Engine.Scripting;

/// <summary>
/// IGamepadSink that drives the native labs PS Remote Play session. Wraps
/// an existing ControllerInput; converts a generic GamepadState into the
/// labs DualSense state passed via labs_session_set_controller_state.
/// </summary>
public sealed class PsRpSink : IGamepadSink
{
    private readonly int _sessionId;
    private readonly ControllerInput _input;

    public PsRpSink(int sessionId, ControllerInput input)
    {
        _sessionId = sessionId;
        _input = input;
    }

    public bool IsConnected(int sessionId) => sessionId == _sessionId;

    public void SendInput(int sessionId, GamepadState state)
    {
        if (sessionId != _sessionId) return;
        var b = state.Buttons;
        bool Get(int i) => i < b.Length && b[i];

        _input.SetButton(LabsNative.LabsControllerButton.CROSS,      Get(0));
        _input.SetButton(LabsNative.LabsControllerButton.MOON,       Get(1));
        _input.SetButton(LabsNative.LabsControllerButton.BOX,        Get(2));
        _input.SetButton(LabsNative.LabsControllerButton.PYRAMID,    Get(3));
        _input.SetButton(LabsNative.LabsControllerButton.L1,         Get(4));
        _input.SetButton(LabsNative.LabsControllerButton.R1,         Get(5));
        _input.SetButton(LabsNative.LabsControllerButton.SHARE,      Get(8));
        _input.SetButton(LabsNative.LabsControllerButton.OPTIONS,    Get(9));
        _input.SetButton(LabsNative.LabsControllerButton.L3,         Get(10));
        _input.SetButton(LabsNative.LabsControllerButton.R3,         Get(11));
        _input.SetButton(LabsNative.LabsControllerButton.DPAD_UP,    Get(12));
        _input.SetButton(LabsNative.LabsControllerButton.DPAD_DOWN,  Get(13));
        _input.SetButton(LabsNative.LabsControllerButton.DPAD_LEFT,  Get(14));
        _input.SetButton(LabsNative.LabsControllerButton.DPAD_RIGHT, Get(15));
        if (b.Length > 16) _input.SetButton(LabsNative.LabsControllerButton.PS, b[16]);

        short ToAxis(float a) => (short)Math.Clamp((int)(a * 32767), -32767, 32767);
        var ax = state.Axes;
        if (ax.Length > 1) _input.SetLeftStick (ToAxis(ax[0]), ToAxis(ax[1]));
        if (ax.Length > 3) _input.SetRightStick(ToAxis(ax[2]), ToAxis(ax[3]));
        _input.SetTriggers(Get(6) ? (byte)255 : (byte)0, Get(7) ? (byte)255 : (byte)0);
    }
}
