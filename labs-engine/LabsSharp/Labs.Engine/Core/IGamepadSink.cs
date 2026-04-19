namespace Labs.Engine.Core;

/// <summary>Abstraction for sending gamepad input to a browser session.</summary>
public interface IGamepadSink
{
    /// <summary>Returns true if the session is connected and ready for input.</summary>
    bool IsConnected(int sessionId);

    /// <summary>Sends gamepad state to the browser. Fire-and-forget; dropped frames are acceptable.</summary>
    void SendInput(int sessionId, GamepadState state);
}
