using System;
using System.Runtime.InteropServices;
using System.Threading;

// XInput test — reads physical controller state and prints live to console.
// Press Ctrl+C to exit.

Console.WriteLine("=== Controller Input Test ===");
Console.WriteLine("Polling XInput for controllers...\n");

var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };

var prev = new XInputState();
int pollCount = 0;

while (!cts.IsCancellationRequested)
{
    bool found = false;
    for (uint i = 0; i < 4; i++)
    {
        var state = new XInputState();
        uint result = XInputGetState(i, ref state);
        if (result != 0) continue;

        found = true;
        if (pollCount % 30 == 0 || StateChanged(prev, state))
        {
            var g = state.Gamepad;
            var buttons = DecodeButtons(g.Buttons);
            Console.Write($"\r[Pad {i}] LX:{g.ThumbLX,7} LY:{g.ThumbLY,7} " +
                          $"RX:{g.ThumbRX,7} RY:{g.ThumbRY,7} " +
                          $"LT:{g.LeftTrigger,3} RT:{g.RightTrigger,3} " +
                          $"Btn:[{buttons}]          ");
            prev = state;
        }
    }

    if (!found && pollCount % 60 == 0)
        Console.Write("\rNo controller detected — plug one in...          ");

    pollCount++;
    Thread.Sleep(16); // ~60Hz
}

Console.WriteLine("\n\nDone.");

static bool StateChanged(XInputState a, XInputState b)
{
    var ga = a.Gamepad; var gb = b.Gamepad;
    return ga.Buttons != gb.Buttons
        || Math.Abs(ga.ThumbLX - gb.ThumbLX) > 1000
        || Math.Abs(ga.ThumbLY - gb.ThumbLY) > 1000
        || Math.Abs(ga.ThumbRX - gb.ThumbRX) > 1000
        || Math.Abs(ga.ThumbRY - gb.ThumbRY) > 1000
        || ga.LeftTrigger != gb.LeftTrigger
        || ga.RightTrigger != gb.RightTrigger;
}

static string DecodeButtons(ushort b)
{
    var parts = new System.Collections.Generic.List<string>();
    if ((b & 0x0001) != 0) parts.Add("UP");
    if ((b & 0x0002) != 0) parts.Add("DOWN");
    if ((b & 0x0004) != 0) parts.Add("LEFT");
    if ((b & 0x0008) != 0) parts.Add("RIGHT");
    if ((b & 0x0010) != 0) parts.Add("START");
    if ((b & 0x0020) != 0) parts.Add("BACK");
    if ((b & 0x0040) != 0) parts.Add("L3");
    if ((b & 0x0080) != 0) parts.Add("R3");
    if ((b & 0x0100) != 0) parts.Add("LB");
    if ((b & 0x0200) != 0) parts.Add("RB");
    if ((b & 0x0400) != 0) parts.Add("GUIDE");
    if ((b & 0x1000) != 0) parts.Add("A");
    if ((b & 0x2000) != 0) parts.Add("B");
    if ((b & 0x4000) != 0) parts.Add("X");
    if ((b & 0x8000) != 0) parts.Add("Y");
    return parts.Count > 0 ? string.Join(" ", parts) : "none";
}

[DllImport("xinput1_4.dll")]
static extern uint XInputGetState(uint dwUserIndex, ref XInputState pState);

[StructLayout(LayoutKind.Sequential)]
struct XInputGamepad
{
    public ushort Buttons;
    public byte LeftTrigger;
    public byte RightTrigger;
    public short ThumbLX;
    public short ThumbLY;
    public short ThumbRX;
    public short ThumbRY;
}

[StructLayout(LayoutKind.Sequential)]
struct XInputState
{
    public uint PacketNumber;
    public XInputGamepad Gamepad;
}
