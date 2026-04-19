using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Labs.Engine.Core;

namespace Labs.RemotePlay.Panels;

public partial class ControllerMonitorView : UserControl
{
    private static Brush? s_off;
    private static Brush? s_on;

    private long _lastTickMs;

    private Brush Off => s_off ??= (Brush)FindResource("MonSurface");
    private Brush On => s_on ??= (Brush)FindResource("MonOn");

    public ControllerMonitorView()
    {
        InitializeComponent();
    }

    /// <summary>Called on any thread; marshals to UI and caps refresh to ~60Hz.</summary>
    public void Update(GamepadState state)
    {
        if (state is null) return;
        var now = Environment.TickCount64;
        if (now - _lastTickMs < 16) return;
        _lastTickMs = now;

        // Snapshot so the UI thread reads coherent values even if the producer
        // mutates the state array mid-dispatch.
        var axes = (float[])state.Axes.Clone();
        var btns = (bool[])state.Buttons.Clone();

        Dispatcher.BeginInvoke(new Action(() => Render(axes, btns)));
    }

    private void Render(float[] axes, bool[] btns)
    {
        const double stickRadius = 28; // (68 - 12) / 2

        Canvas.SetLeft(LeftStickDot,  stickRadius + axes[0] * stickRadius);
        Canvas.SetTop (LeftStickDot,  stickRadius + axes[1] * stickRadius);
        Canvas.SetLeft(RightStickDot, stickRadius + axes[2] * stickRadius);
        Canvas.SetTop (RightStickDot, stickRadius + axes[3] * stickRadius);

        // Triggers: buttons[6] = L2, buttons[7] = R2 (binary in our pipeline)
        L2Bar.Height = btns[6] ? 62 : 0;
        R2Bar.Height = btns[7] ? 62 : 0;

        Set(LedA,      btns[0]);
        Set(LedB,      btns[1]);
        Set(LedX,      btns[2]);
        Set(LedY,      btns[3]);
        Set(LedLB,     btns[4]);
        Set(LedRB,     btns[5]);
        Set(LedBack,   btns[8]);
        Set(LedStart,  btns[9]);
        Set(LedL3,     btns[10]);
        Set(LedR3,     btns[11]);
        Set(LedDUp,    btns[12]);
        Set(LedDDown,  btns[13]);
        Set(LedDLeft,  btns[14]);
        Set(LedDRight, btns[15]);
        Set(LedGuide,  btns[16]);
    }

    private void Set(System.Windows.Shapes.Ellipse e, bool on)
        => e.Fill = on ? On : Off;

    /// <summary>Resets all LEDs to off and sticks to center. Call when no source is bound.</summary>
    public void Clear()
    {
        Dispatcher.BeginInvoke(new Action(() => Render(new float[4], new bool[17])));
    }
}
