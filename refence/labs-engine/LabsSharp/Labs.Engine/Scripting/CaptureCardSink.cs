using System;
using Labs.Engine.Core;

namespace Labs.Engine.Scripting;

/// <summary>
/// IGamepadSink that drives a ViGEm virtual Xbox 360 pad. Used for capture-card
/// sessions where the game runs locally on the PC and reads the pad like a
/// physical controller.
/// </summary>
public sealed class CaptureCardSink : IGamepadSink, IDisposable
{
    private readonly VirtualPadManager _pads = new();
    private readonly int _sessionId;
    private bool _ready;

    public CaptureCardSink(int sessionId)
    {
        _sessionId = sessionId;
        try
        {
            _pads.Initialize();
            _pads.ConnectPad(sessionId);
            _ready = true;
        }
        catch (VirtualPadException ex)
        {
            Console.WriteLine("[CaptureCardSink] ViGEm unavailable: " + ex.Message);
        }
    }

    public bool IsConnected(int sessionId) => _ready && sessionId == _sessionId;

    public void SendInput(int sessionId, GamepadState state)
    {
        if (!_ready || sessionId != _sessionId) return;
        var evt = ToEvent(state);
        try { _pads.ApplyEvent(sessionId, evt); } catch { }
    }

    // GamepadState: Axes[0..3] = LX, LY, RX, RY (-1..1); Buttons[0..16] xinput-style
    private static GamepadEvent ToEvent(GamepadState s)
    {
        var b = s.Buttons;
        ushort mask = 0;
        if (b.Length > 0  && b[0])  mask |= Xbox360Buttons.A;
        if (b.Length > 1  && b[1])  mask |= Xbox360Buttons.B;
        if (b.Length > 2  && b[2])  mask |= Xbox360Buttons.X;
        if (b.Length > 3  && b[3])  mask |= Xbox360Buttons.Y;
        if (b.Length > 4  && b[4])  mask |= Xbox360Buttons.LeftShoulder;
        if (b.Length > 5  && b[5])  mask |= Xbox360Buttons.RightShoulder;
        if (b.Length > 8  && b[8])  mask |= Xbox360Buttons.Back;
        if (b.Length > 9  && b[9])  mask |= Xbox360Buttons.Start;
        if (b.Length > 10 && b[10]) mask |= Xbox360Buttons.LeftThumb;
        if (b.Length > 11 && b[11]) mask |= Xbox360Buttons.RightThumb;
        if (b.Length > 12 && b[12]) mask |= Xbox360Buttons.Up;
        if (b.Length > 13 && b[13]) mask |= Xbox360Buttons.Down;
        if (b.Length > 14 && b[14]) mask |= Xbox360Buttons.Left;
        if (b.Length > 15 && b[15]) mask |= Xbox360Buttons.Right;

        return new GamepadEvent
        {
            LeftStickX  = s.Axes.Length > 0 ? s.Axes[0] : 0f,
            LeftStickY  = s.Axes.Length > 1 ? s.Axes[1] : 0f,
            RightStickX = s.Axes.Length > 2 ? s.Axes[2] : 0f,
            RightStickY = s.Axes.Length > 3 ? s.Axes[3] : 0f,
            LeftTrigger  = (b.Length > 6 && b[6]) ? 1f : 0f,
            RightTrigger = (b.Length > 7 && b[7]) ? 1f : 0f,
            Buttons = mask,
        };
    }

    public void Dispose() => _pads.Dispose();
}
