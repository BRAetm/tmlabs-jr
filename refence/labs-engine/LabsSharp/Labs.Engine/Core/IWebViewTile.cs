using System;
using System.Threading.Tasks;

namespace Labs.Engine.Core;

/// <summary>Interface for a UI tile that can host a WebView2 session.</summary>
public interface IWebViewTile
{
    /// <summary>Initializes WebView2 and navigates to the URL.</summary>
    Task InitWebViewAsync(string url);

    /// <summary>Sends gamepad state to the browser.</summary>
    void SendGamepadInput(GamepadState state);

    /// <summary>True when WebView2 is initialized and ready.</summary>
    bool IsWebViewReady { get; }

    /// <summary>Destroys the WebView2 and cleans up resources.</summary>
    void DestroyWebView();

    /// <summary>Displays a CV-annotated frame overlay on the tile.</summary>
    void ShowCvFrame(byte[] jpegBytes);

    /// <summary>Hides the CV overlay.</summary>
    void HideCvOverlay();

    /// <summary>Starts CDP Page.startScreencast to stream game frames.</summary>
    Task StartScreencastAsync(int sessionId, int maxFps, int quality, Action<int, byte[]> onFrame);

    /// <summary>Stops CDP screencast.</summary>
    Task StopScreencastAsync();

    /// <summary>Mutes or unmutes the WebView2 audio.</summary>
    void SetMuted(bool muted);

    /// <summary>True if the WebView2 audio is currently muted.</summary>
    bool IsMuted { get; }
}
