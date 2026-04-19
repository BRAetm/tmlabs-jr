using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace Labs.RemotePlay.Theme;

internal static class DarkTitleBar
{
    // Dark immersive title bar — value 20 on Win10 build 18985+ / Win11; 19 on older builds.
    private const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
    private const int DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19;

    // Rounded corner preference (Windows 11 22000+). Silently ignored on Win10.
    //   0 = default, 1 = do not round, 2 = round, 3 = round small
    private const int DWMWA_WINDOW_CORNER_PREFERENCE = 33;
    private const int DWMWCP_ROUND = 2;

    // Border color for the 1px window outline Win11 draws (22000+).
    private const int DWMWA_BORDER_COLOR = 34;

    [DllImport("dwmapi.dll", PreserveSig = true)]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int attrValue, int attrSize);

    public static void Apply(Window window)
    {
        void hook(object? s, EventArgs e)
        {
            var hwnd = new WindowInteropHelper(window).Handle;
            if (hwnd == IntPtr.Zero) return;

            int on = 1;
            if (DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref on, sizeof(int)) != 0)
                DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, ref on, sizeof(int));

            int round = DWMWCP_ROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, ref round, sizeof(int));

            // Subtle accent-tinted border — COLORREF 0x00BBGGRR. #2E3F6A → 0x006A3F2E
            int border = 0x006A3F2E;
            DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, ref border, sizeof(int));
        }
        if (window.IsLoaded) hook(null, EventArgs.Empty);
        else window.SourceInitialized += hook;
    }
}
