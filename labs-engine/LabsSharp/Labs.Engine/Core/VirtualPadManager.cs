using System;
using System.Collections.Generic;
using Nefarius.ViGEm.Client;
using Nefarius.ViGEm.Client.Targets;
using Nefarius.ViGEm.Client.Targets.Xbox360;

namespace Labs.Engine.Core;

// ---------------------------------------------------------------------------
// Data Model
// ---------------------------------------------------------------------------

/// <summary>Gamepad event emitted from a Python CV worker via ZMQ, deserialized from JSON.</summary>
public class GamepadEvent
{
    public float LeftStickX   { get; set; }  // -1.0 to 1.0
    public float LeftStickY   { get; set; }  // -1.0 to 1.0
    public float RightStickX  { get; set; }  // -1.0 to 1.0
    public float RightStickY  { get; set; }  // -1.0 to 1.0
    public float LeftTrigger  { get; set; }  // 0.0 to 1.0
    public float RightTrigger { get; set; }  // 0.0 to 1.0

    // Raw Xbox 360 button bitmask — use Xbox360Buttons constants to compose
    // e.g. Xbox360Buttons.A | Xbox360Buttons.B
    public ushort Buttons { get; set; }
}

/// <summary>Gamepad state using the Web Gamepad API format — 4 axes + 17 buttons.</summary>
public class GamepadState
{
    /// <summary>Axes: [0]=LeftStickX, [1]=LeftStickY, [2]=RightStickX, [3]=RightStickY. Range -1.0 to 1.0.</summary>
    public float[] Axes { get; set; } = new float[4];

    /// <summary>Standard Gamepad buttons 0–16. True = pressed.</summary>
    public bool[] Buttons { get; set; } = new bool[17];
}

/// <summary>Xbox 360 button bitmask constants matching the ViGEm wire format.</summary>
public static class Xbox360Buttons
{
    public const ushort Up           = 0x0001;
    public const ushort Down         = 0x0002;
    public const ushort Left         = 0x0004;
    public const ushort Right        = 0x0008;
    public const ushort Start        = 0x0010;
    public const ushort Back         = 0x0020;
    public const ushort LeftThumb    = 0x0040;
    public const ushort RightThumb   = 0x0080;
    public const ushort LeftShoulder = 0x0100;
    public const ushort RightShoulder= 0x0200;
    public const ushort Guide        = 0x0400;
    public const ushort A            = 0x1000;
    public const ushort B            = 0x2000;
    public const ushort X            = 0x4000;
    public const ushort Y            = 0x8000;
}

// ---------------------------------------------------------------------------
// Exception
// ---------------------------------------------------------------------------

/// <summary>Thrown when ViGEmBus driver is unavailable or a pad operation fails.</summary>
public class VirtualPadException : Exception
{
    /// <summary>Session ID that caused the failure, or -1 for driver-level failures.</summary>
    public int SessionId { get; init; }

    public VirtualPadException(int sessionId, string message, Exception? inner = null)
        : base(message, inner)
    {
        SessionId = sessionId;
    }
}

// ---------------------------------------------------------------------------
// VirtualPadManager
// ---------------------------------------------------------------------------

/// <summary>Creates and manages ViGEm virtual Xbox 360 controllers per session.</summary>
public class VirtualPadManager : IDisposable
{
    private const int MinSessionId = 0;
    private const int MaxSessionId = 9;

    private ViGEmClient? _client;
    private readonly Dictionary<int, IXbox360Controller> _pads = new();
    private readonly object _padsLock = new();
    private bool _disposed;

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------

    /// <summary>Connects to ViGEmBus driver; throws VirtualPadException if driver is missing.</summary>
    public void Initialize()
    {
        try
        {
            _client = new ViGEmClient();
            Console.WriteLine("[VirtualPadManager] ViGEmBus driver connected.");
        }
        catch (Exception ex)
        {
            throw new VirtualPadException(-1, "ViGEmBus driver is not installed or failed to connect.", ex);
        }
    }

    /// <summary>Creates and plugs in a virtual Xbox 360 pad for the given session ID.</summary>
    public void ConnectPad(int sessionId)
    {
        ValidateSessionId(sessionId);

        if (_client is null)
            throw new InvalidOperationException("VirtualPadManager is not initialized. Call Initialize() first.");

        try
        {
            var pad = _client.CreateXbox360Controller();
            pad.AutoSubmitReport = false;
            pad.Connect();

            lock (_padsLock)
                _pads[sessionId] = pad;

            Console.WriteLine($"[VirtualPadManager] Session {sessionId}: pad connected.");
        }
        catch (VirtualPadException)
        {
            throw;
        }
        catch (Exception ex)
        {
            HandleViGEmError(sessionId, nameof(ConnectPad), ex);
            throw new VirtualPadException(sessionId, $"Failed to connect pad for session {sessionId}.", ex);
        }
    }

    /// <summary>Maps a GamepadEvent to controller state and calls SubmitReport() in one shot.</summary>
    public void ApplyEvent(int sessionId, GamepadEvent evt)
    {
        ValidateSessionId(sessionId);

        IXbox360Controller? pad;
        lock (_padsLock)
            _pads.TryGetValue(sessionId, out pad);

        if (pad is null)
        {
            Console.WriteLine($"[VirtualPadManager] Session {sessionId}: ApplyEvent called but no pad connected — dropping frame.");
            return;
        }

        try
        {
            pad.SetAxisValue(Xbox360Axis.LeftThumbX,  ToStickShort(evt.LeftStickX));
            pad.SetAxisValue(Xbox360Axis.LeftThumbY,  ToStickShort(evt.LeftStickY));
            pad.SetAxisValue(Xbox360Axis.RightThumbX, ToStickShort(evt.RightStickX));
            pad.SetAxisValue(Xbox360Axis.RightThumbY, ToStickShort(evt.RightStickY));
            pad.SetSliderValue(Xbox360Slider.LeftTrigger,  ToTriggerByte(evt.LeftTrigger));
            pad.SetSliderValue(Xbox360Slider.RightTrigger, ToTriggerByte(evt.RightTrigger));
            pad.SetButtonsFull(evt.Buttons);
            pad.SubmitReport();
        }
        catch (Exception ex)
        {
            HandleViGEmError(sessionId, nameof(ApplyEvent), ex);
            // Drop frame — do not rethrow
        }
    }

    /// <summary>Unplugs and disposes the pad for the given session ID.</summary>
    public void DisconnectPad(int sessionId)
    {
        ValidateSessionId(sessionId);

        IXbox360Controller? pad;
        lock (_padsLock)
        {
            _pads.TryGetValue(sessionId, out pad);
            _pads.Remove(sessionId);
        }

        if (pad is null)
        {
            Console.WriteLine($"[VirtualPadManager] Session {sessionId}: DisconnectPad called but no pad found — no-op.");
            return;
        }

        try
        {
            pad.Disconnect();
            Console.WriteLine($"[VirtualPadManager] Session {sessionId}: pad disconnected.");
        }
        catch (Exception ex)
        {
            HandleViGEmError(sessionId, nameof(DisconnectPad), ex);
            // Always removed from dictionary above — continue regardless
        }
    }

    /// <summary>Disconnects all pads and disposes the ViGEmClient.</summary>
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        List<int> sessionIds;
        lock (_padsLock)
            sessionIds = new List<int>(_pads.Keys);

        foreach (var id in sessionIds)
        {
            try
            {
                if (_pads.TryGetValue(id, out var pad))
                    pad.Disconnect();
            }
            catch (Exception ex)
            {
                HandleViGEmError(id, nameof(Dispose), ex);
                // Swallow — attempt all pads regardless
            }
        }

        lock (_padsLock)
            _pads.Clear();

        _client?.Dispose();
        Console.WriteLine("[VirtualPadManager] All pads disconnected and ViGEmClient disposed.");
    }

    // ---------------------------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------------------------

    /// <summary>Converts a normalized stick float (-1..1) to a signed short (-32768..32767).</summary>
    private static short ToStickShort(float value)
    {
        var clamped = Math.Clamp(value, -1f, 1f);
        return (short)(clamped * short.MaxValue);
    }

    /// <summary>Converts a normalized trigger float (0..1) to a byte (0..255).</summary>
    private static byte ToTriggerByte(float value)
    {
        var clamped = Math.Clamp(value, 0f, 1f);
        return (byte)(clamped * byte.MaxValue);
    }

    /// <summary>Verifies session ID is in range 0–9.</summary>
    private static void ValidateSessionId(int sessionId)
    {
        if (sessionId < MinSessionId || sessionId > MaxSessionId)
            throw new ArgumentOutOfRangeException(nameof(sessionId),
                $"Session ID must be {MinSessionId}–{MaxSessionId}, got {sessionId}.");
    }

    /// <summary>Logs a ViGEm error for the given session and operation.</summary>
    private static void HandleViGEmError(int sessionId, string operation, Exception ex)
    {
        Console.WriteLine($"[VirtualPadManager] ERROR — Session {sessionId}, {operation}: {ex.Message}");
    }
}
