using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Labs.Engine.Core;

namespace Labs.RemotePlay.Panels;

/// <summary>Full-size controller visualization used on the "controller monitor" tab.
/// Same public API as <see cref="ControllerMonitorView"/> so either can be driven from
/// the same state source.</summary>
public partial class ControllerMonitorFullView : UserControl
{
    private Brush? _off;
    private Brush? _on;
    private long _lastTickMs;

    private Brush Off => _off ??= (Brush)FindResource("FCtlSurface");
    private Brush On  => _on  ??= (Brush)FindResource("FCtlOn");

    public ControllerMonitorFullView() => InitializeComponent();

    public void Update(GamepadState state)
    {
        if (state is null) return;
        var now = Environment.TickCount64;
        if (now - _lastTickMs < 16) return;
        _lastTickMs = now;

        var axes = (float[])state.Axes.Clone();
        var btns = (bool[])state.Buttons.Clone();

        Dispatcher.BeginInvoke(new Action(() => Render(axes, btns)));
    }

    private void Render(float[] axes, bool[] btns)
    {
        const double stickRadius = 27; // (68 - 14) / 2

        Canvas.SetLeft(LeftStickDot,  stickRadius + axes[0] * stickRadius);
        Canvas.SetTop (LeftStickDot,  stickRadius + axes[1] * stickRadius);
        Canvas.SetLeft(RightStickDot, stickRadius + axes[2] * stickRadius);
        Canvas.SetTop (RightStickDot, stickRadius + axes[3] * stickRadius);

        L2Bar.Height = btns[6] ? 46 : 0;
        R2Bar.Height = btns[7] ? 46 : 0;

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

    private void Set(System.Windows.Shapes.Ellipse e, bool on) => e.Fill = on ? On : Off;

    public void Clear() => Dispatcher.BeginInvoke(new Action(() => Render(new float[4], new bool[17])));
}
