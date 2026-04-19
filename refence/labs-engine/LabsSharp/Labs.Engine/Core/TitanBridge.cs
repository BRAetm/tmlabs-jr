using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using HidSharp;

namespace Labs.Engine.Core;

/// <summary>Supported Titan/Cronus device types.</summary>
public enum TitanDeviceType { TitanTwo, CronusZen, CronusMax, Unknown }

/// <summary>Info about a detected Titan/Cronus device.</summary>
public record TitanDeviceInfo(string Path, string Name, TitanDeviceType Type, int VendorId, int ProductId);

/// <summary>
/// USB HID bridge for Titan Two and Cronus Zen/Max devices.
/// Sends gamepad state as HID output reports matching the device's expected format.
/// Ported from Helios TitanBridge.dll + hidapi.dll architecture.
/// </summary>
public class TitanBridge : IDisposable
{
    // Known Titan/Cronus USB Vendor IDs
    private const int VID_TITAN_TWO   = 0x2508;
    private const int VID_CRONUS_ZEN  = 0x2E8A;
    private const int VID_CRONUS_MAX  = 0x2508; // Same VID, different PID

    // Known Product IDs
    private const int PID_TITAN_TWO     = 0x0002;
    private const int PID_CRONUS_ZEN    = 0x000A;
    private const int PID_CRONUS_MAX_V3 = 0x0001;

    // Report size for gamepad state (standard Xbox-style report)
    private const int REPORT_SIZE = 36;

    private HidDevice? _device;
    private HidStream? _stream;
    private TitanDeviceInfo? _connectedDevice;
    private readonly object _lock = new();
    private bool _disposed;

    // ---------------------------------------------------------------------------
    // Device Discovery
    // ---------------------------------------------------------------------------

    /// <summary>Scans for connected Titan Two / Cronus Zen / Cronus Max devices.</summary>
    public static List<TitanDeviceInfo> ScanDevices()
    {
        var results = new List<TitanDeviceInfo>();

        try
        {
            var devices = DeviceList.Local.GetHidDevices();

            foreach (var dev in devices)
            {
                var type = ClassifyDevice(dev.VendorID, dev.ProductID);
                if (type == TitanDeviceType.Unknown) continue;

                var name = dev.GetFriendlyName() ?? $"{type} ({dev.VendorID:X4}:{dev.ProductID:X4})";
                results.Add(new TitanDeviceInfo(dev.DevicePath, name, type, dev.VendorID, dev.ProductID));
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[TitanBridge] Device scan error: {ex.Message}");
        }

        return results;
    }

    /// <summary>Returns true if any Titan/Cronus device is connected to USB.</summary>
    public static bool IsAnyDeviceAvailable() => ScanDevices().Count > 0;

    // ---------------------------------------------------------------------------
    // Connection
    // ---------------------------------------------------------------------------

    /// <summary>Connects to the first available Titan/Cronus device.</summary>
    public bool ConnectAuto()
    {
        var devices = ScanDevices();
        if (devices.Count == 0)
        {
            Console.WriteLine("[TitanBridge] No Titan/Cronus device found.");
            return false;
        }
        return Connect(devices[0]);
    }

    /// <summary>Connects to a specific device.</summary>
    public bool Connect(TitanDeviceInfo deviceInfo)
    {
        lock (_lock)
        {
            Disconnect();

            try
            {
                var hidDevices = DeviceList.Local.GetHidDevices()
                    .Where(d => d.DevicePath == deviceInfo.Path)
                    .ToList();

                if (hidDevices.Count == 0)
                {
                    Console.WriteLine($"[TitanBridge] Device not found: {deviceInfo.Name}");
                    return false;
                }

                _device = hidDevices[0];
                _stream = _device.Open();
                _stream.WriteTimeout = 100;
                _connectedDevice = deviceInfo;

                Console.WriteLine($"[TitanBridge] Connected to {deviceInfo.Name} ({deviceInfo.Type})");
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[TitanBridge] Connect failed: {ex.Message}");
                _device = null;
                _stream = null;
                _connectedDevice = null;
                return false;
            }
        }
    }

    /// <summary>Disconnects from the current device.</summary>
    public void Disconnect()
    {
        lock (_lock)
        {
            if (_stream is not null)
            {
                try { _stream.Close(); } catch { }
                _stream = null;
            }
            _device = null;

            if (_connectedDevice is not null)
            {
                Console.WriteLine($"[TitanBridge] Disconnected from {_connectedDevice.Name}");
                _connectedDevice = null;
            }
        }
    }

    /// <summary>True if currently connected to a device.</summary>
    public bool IsConnected
    {
        get { lock (_lock) return _stream is not null && _connectedDevice is not null; }
    }

    /// <summary>Info about the currently connected device, or null.</summary>
    public TitanDeviceInfo? ConnectedDevice
    {
        get { lock (_lock) return _connectedDevice; }
    }

    // ---------------------------------------------------------------------------
    // Gamepad State Transmission
    // ---------------------------------------------------------------------------

    /// <summary>Sends a GamepadState to the connected Titan/Cronus device as an HID output report.</summary>
    public bool SendState(GamepadState state)
    {
        lock (_lock)
        {
            if (_stream is null) return false;

            try
            {
                var report = BuildReport(state);
                _stream.Write(report);
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[TitanBridge] SendState error: {ex.Message}");
                return false;
            }
        }
    }

    /// <summary>Sends a GamepadEvent (ViGEm format) to the device.</summary>
    public bool SendEvent(GamepadEvent evt)
    {
        lock (_lock)
        {
            if (_stream is null) return false;

            try
            {
                var report = BuildReportFromEvent(evt);
                _stream.Write(report);
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[TitanBridge] SendEvent error: {ex.Message}");
                return false;
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Report Building
    // ---------------------------------------------------------------------------

    /// <summary>Builds a HID output report from Web Gamepad API state (buttons[17] + axes[4]).</summary>
    private static byte[] BuildReport(GamepadState state)
    {
        var report = new byte[REPORT_SIZE];
        report[0] = 0x00; // Report ID

        // Button bitmask (bytes 1-2, little-endian)
        ushort buttons = 0;
        // Web Gamepad → Xbox mapping
        if (state.Buttons[0])  buttons |= 0x1000; // A
        if (state.Buttons[1])  buttons |= 0x2000; // B
        if (state.Buttons[2])  buttons |= 0x4000; // X
        if (state.Buttons[3])  buttons |= 0x8000; // Y
        if (state.Buttons[4])  buttons |= 0x0100; // LB
        if (state.Buttons[5])  buttons |= 0x0200; // RB
        if (state.Buttons[8])  buttons |= 0x0020; // Back
        if (state.Buttons[9])  buttons |= 0x0010; // Start
        if (state.Buttons[10]) buttons |= 0x0040; // LS Click
        if (state.Buttons[11]) buttons |= 0x0080; // RS Click
        if (state.Buttons[12]) buttons |= 0x0001; // DPad Up
        if (state.Buttons[13]) buttons |= 0x0002; // DPad Down
        if (state.Buttons[14]) buttons |= 0x0004; // DPad Left
        if (state.Buttons[15]) buttons |= 0x0008; // DPad Right
        if (state.Buttons[16]) buttons |= 0x0400; // Guide

        report[1] = (byte)(buttons & 0xFF);
        report[2] = (byte)((buttons >> 8) & 0xFF);

        // Left trigger (byte 3, 0-255)
        report[3] = state.Buttons[6] ? (byte)255 : (byte)0;
        // Right trigger (byte 4, 0-255)
        report[4] = state.Buttons[7] ? (byte)255 : (byte)0;

        // Left stick X (bytes 5-6, signed short, little-endian)
        short lx = (short)(Math.Clamp(state.Axes[0], -1f, 1f) * short.MaxValue);
        report[5] = (byte)(lx & 0xFF);
        report[6] = (byte)((lx >> 8) & 0xFF);

        // Left stick Y (bytes 7-8)
        short ly = (short)(Math.Clamp(state.Axes[1], -1f, 1f) * short.MaxValue);
        report[7] = (byte)(ly & 0xFF);
        report[8] = (byte)((ly >> 8) & 0xFF);

        // Right stick X (bytes 9-10)
        short rx = (short)(Math.Clamp(state.Axes[2], -1f, 1f) * short.MaxValue);
        report[9] = (byte)(rx & 0xFF);
        report[10] = (byte)((rx >> 8) & 0xFF);

        // Right stick Y (bytes 11-12)
        short ry = (short)(Math.Clamp(state.Axes[3], -1f, 1f) * short.MaxValue);
        report[11] = (byte)(ry & 0xFF);
        report[12] = (byte)((ry >> 8) & 0xFF);

        return report;
    }

    /// <summary>Builds a HID report from a ViGEm-format GamepadEvent.</summary>
    private static byte[] BuildReportFromEvent(GamepadEvent evt)
    {
        var report = new byte[REPORT_SIZE];
        report[0] = 0x00; // Report ID

        // Buttons (already a bitmask)
        report[1] = (byte)(evt.Buttons & 0xFF);
        report[2] = (byte)((evt.Buttons >> 8) & 0xFF);

        // Triggers
        report[3] = (byte)(Math.Clamp(evt.LeftTrigger, 0f, 1f) * 255);
        report[4] = (byte)(Math.Clamp(evt.RightTrigger, 0f, 1f) * 255);

        // Sticks
        short lx = (short)(Math.Clamp(evt.LeftStickX, -1f, 1f) * short.MaxValue);
        short ly = (short)(Math.Clamp(evt.LeftStickY, -1f, 1f) * short.MaxValue);
        short rx = (short)(Math.Clamp(evt.RightStickX, -1f, 1f) * short.MaxValue);
        short ry = (short)(Math.Clamp(evt.RightStickY, -1f, 1f) * short.MaxValue);

        report[5]  = (byte)(lx & 0xFF); report[6]  = (byte)((lx >> 8) & 0xFF);
        report[7]  = (byte)(ly & 0xFF); report[8]  = (byte)((ly >> 8) & 0xFF);
        report[9]  = (byte)(rx & 0xFF); report[10] = (byte)((rx >> 8) & 0xFF);
        report[11] = (byte)(ry & 0xFF); report[12] = (byte)((ry >> 8) & 0xFF);

        return report;
    }

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    private static TitanDeviceType ClassifyDevice(int vid, int pid)
    {
        if (vid == VID_TITAN_TWO && pid == PID_TITAN_TWO)
            return TitanDeviceType.TitanTwo;
        if (vid == VID_CRONUS_ZEN && pid == PID_CRONUS_ZEN)
            return TitanDeviceType.CronusZen;
        if (vid == VID_TITAN_TWO && pid == PID_CRONUS_MAX_V3)
            return TitanDeviceType.CronusMax;

        // Broader match: any device from these vendors
        if (vid == VID_TITAN_TWO) return TitanDeviceType.TitanTwo;
        if (vid == VID_CRONUS_ZEN) return TitanDeviceType.CronusZen;

        return TitanDeviceType.Unknown;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Disconnect();
    }
}
