using System;
using Labs.Engine.Core.Shm;

namespace Labs.Engine.Core;

/// <summary>
/// Fan-out sink: forwards gamepad state to the primary session sink (ViGEm/remote play),
/// mirrors into the Helios-convention SHM block so a running GPC3 VM can read live input,
/// and optionally drives a physical Titan Two in parallel.
/// </summary>
public sealed class FanOutSink : IGamepadSink
{
    private readonly IGamepadSink _primary;
    private readonly HeliosCompatShmBridge _helios;
    private readonly Func<TitanBridge?> _titan;

    public FanOutSink(IGamepadSink primary, HeliosCompatShmBridge helios, Func<TitanBridge?> titan)
    {
        _primary = primary;
        _helios = helios;
        _titan = titan;
    }

    public bool IsConnected(int sessionId) => _primary.IsConnected(sessionId);

    /// <summary>Fires after every forwarded state. Subscribe for UI monitors. Non-blocking; exceptions are swallowed.</summary>
    public event Action<GamepadState>? StateObserved;

    public void SendInput(int sessionId, GamepadState state)
    {
        try { _primary.SendInput(sessionId, state); } catch { }
        try { _helios.PublishInput(state); } catch { }
        var t = _titan();
        if (t is not null) { try { t.SendState(state); } catch { } }
        try { StateObserved?.Invoke(state); } catch { }
    }
}
