using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Labs.Engine.Core;

/// <summary>
/// Injects keyboard input directly into a browser HWND via PostMessage,
/// without requiring window focus. Maps GamepadEvent fields to Xbox Cloud
/// Gaming keyboard shortcuts and tracks per-session key state for correct
/// WM_KEYDOWN / WM_KEYUP pairing.
/// </summary>
public class BrowserInputInjector
{
    // Win32 message constants
    private const uint WmKeydown = 0x0100;
    private const uint WmKeyup   = 0x0101;

    // MapVirtualKey translation mode: VK → scan code
    private const uint MapvkVkToVsc = 0;

    // Axis dead-zone threshold
    private const float StickThreshold   = 0.3f;
    private const float TriggerThreshold = 0.5f;

    // Virtual key codes
    private const byte VkSpace   = 0x20;
    private const byte VkReturn  = 0x0D;
    private const byte VkLeft    = 0x25;
    private const byte VkUp      = 0x26;
    private const byte VkRight   = 0x27;
    private const byte VkDown    = 0x28;
    private const byte VkA       = 0x41;
    private const byte VkB       = 0x42;
    private const byte VkD       = 0x44;
    private const byte VkE       = 0x45;
    private const byte VkQ       = 0x51;
    private const byte VkS       = 0x53;
    private const byte VkW       = 0x57;
    private const byte VkX       = 0x58;
    private const byte VkY       = 0x59;
    private const byte VkOem4    = 0xDB; // [ left bracket  — LB
    private const byte VkOem6    = 0xDD; // ] right bracket — RB

    // Button bitmask constants (matching Xbox360Buttons in VirtualPadManager)
    private const ushort BtnDpadUp    = 0x0001;
    private const ushort BtnDpadDown  = 0x0002;
    private const ushort BtnDpadLeft  = 0x0004;
    private const ushort BtnDpadRight = 0x0008;
    private const ushort BtnStart     = 0x0010;
    private const ushort BtnLb        = 0x0100;
    private const ushort BtnRb        = 0x0200;
    private const ushort BtnA         = 0x1000;
    private const ushort BtnB         = 0x2000;
    private const ushort BtnX         = 0x4000;
    private const ushort BtnY         = 0x8000;

    // Per-session set of VK codes currently held down
    private readonly Dictionary<int, HashSet<byte>> _heldKeys = new();
    private readonly object _lock = new();

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------

    /// <summary>
    /// Translates a GamepadEvent to keyboard input and injects WM_KEYDOWN /
    /// WM_KEYUP messages into the browser window without requiring focus.
    /// </summary>
    public void InjectInput(int sessionId, nint hwnd, GamepadEvent evt)
    {
        var desired = BuildDesiredKeySet(evt);

        HashSet<byte> held;
        lock (_lock)
        {
            if (!_heldKeys.TryGetValue(sessionId, out held!))
            {
                held = new HashSet<byte>();
                _heldKeys[sessionId] = held;
            }
        }

        // Keys newly pressed this frame
        foreach (var vk in desired)
            if (!held.Contains(vk))
                PostKey(hwnd, vk, WmKeydown);

        // Keys released this frame
        foreach (var vk in held)
            if (!desired.Contains(vk))
                PostKey(hwnd, vk, WmKeyup);

        lock (_lock)
        {
            held.Clear();
            held.UnionWith(desired);
        }
    }

    /// <summary>Releases all held keys for the given session — call on session stop.</summary>
    public void ReleaseAll(int sessionId, nint hwnd)
    {
        HashSet<byte>? held;
        lock (_lock)
        {
            _heldKeys.TryGetValue(sessionId, out held);
            _heldKeys.Remove(sessionId);
        }

        if (held is null) return;
        foreach (var vk in held)
            PostKey(hwnd, vk, WmKeyup);
    }

    // ---------------------------------------------------------------------------
    // Mapping
    // ---------------------------------------------------------------------------

    /// <summary>Builds the set of VK codes that should be held down for this frame.</summary>
    private static HashSet<byte> BuildDesiredKeySet(GamepadEvent evt)
    {
        var keys = new HashSet<byte>();

        // --- Buttons ---
        if ((evt.Buttons & BtnA)        != 0) keys.Add(VkSpace);
        if ((evt.Buttons & BtnB)        != 0) keys.Add(VkB);
        if ((evt.Buttons & BtnX)        != 0) keys.Add(VkX);
        if ((evt.Buttons & BtnY)        != 0) keys.Add(VkY);
        if ((evt.Buttons & BtnStart)    != 0) keys.Add(VkReturn);
        if ((evt.Buttons & BtnLb)       != 0) keys.Add(VkOem4);
        if ((evt.Buttons & BtnRb)       != 0) keys.Add(VkOem6);

        // --- D-Pad ---
        if ((evt.Buttons & BtnDpadUp)   != 0) keys.Add(VkUp);
        if ((evt.Buttons & BtnDpadDown) != 0) keys.Add(VkDown);
        if ((evt.Buttons & BtnDpadLeft) != 0) keys.Add(VkLeft);
        if ((evt.Buttons & BtnDpadRight)!= 0) keys.Add(VkRight);

        // --- Left stick ---
        if (evt.LeftStickY < -StickThreshold) keys.Add(VkW);
        if (evt.LeftStickY >  StickThreshold) keys.Add(VkS);
        if (evt.LeftStickX < -StickThreshold) keys.Add(VkA);
        if (evt.LeftStickX >  StickThreshold) keys.Add(VkD);

        // --- Right stick ---
        if (evt.RightStickX < -StickThreshold) keys.Add(VkLeft);
        if (evt.RightStickX >  StickThreshold) keys.Add(VkRight);
        if (evt.RightStickY < -StickThreshold) keys.Add(VkUp);
        if (evt.RightStickY >  StickThreshold) keys.Add(VkDown);

        // --- Triggers ---
        if (evt.LeftTrigger  > TriggerThreshold) keys.Add(VkQ);
        if (evt.RightTrigger > TriggerThreshold) keys.Add(VkE);

        return keys;
    }

    // ---------------------------------------------------------------------------
    // PostMessage helpers
    // ---------------------------------------------------------------------------

    /// <summary>Posts a single WM_KEYDOWN or WM_KEYUP to the target window.</summary>
    private static void PostKey(nint hwnd, byte vk, uint message)
    {
        uint scanCode = MapVirtualKey(vk, MapvkVkToVsc);
        nint lParam   = message == WmKeydown
            ? BuildKeydownLParam(scanCode)
            : BuildKeyupLParam(scanCode);

        PostMessage(hwnd, message, (nint)vk, lParam);
    }

    /// <summary>Builds the lParam for WM_KEYDOWN: repeat count 1, scan code, all other bits 0.</summary>
    private static nint BuildKeydownLParam(uint scanCode) =>
        (nint)((scanCode << 16) | 1u);

    /// <summary>
    /// Builds the lParam for WM_KEYUP: repeat count 1, scan code,
    /// bit 30 (previous key state = down) and bit 31 (transition = releasing) set.
    /// </summary>
    private static nint BuildKeyupLParam(uint scanCode) =>
        (nint)((scanCode << 16) | 0xC0000001u);

    // ---------------------------------------------------------------------------
    // P/Invoke
    // ---------------------------------------------------------------------------

    [DllImport("user32.dll")]
    private static extern bool PostMessage(nint hWnd, uint msg, nint wParam, nint lParam);

    [DllImport("user32.dll")]
    private static extern uint MapVirtualKey(uint uCode, uint uMapType);
}
