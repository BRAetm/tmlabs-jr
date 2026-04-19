using System;
using System.Threading.Tasks;

namespace Labs.Engine.Core;

/// <summary>Input source type for a session.</summary>
public enum InputSourceType
{
    /// <summary>WebView2 embedded browser (Xbox Cloud Gaming, PS Remote Play web, custom URL).</summary>
    WebView2,
    /// <summary>Capture card via DirectShow/OpenCV (Elgato, Magewell, USB capture).</summary>
    CaptureCard,
    /// <summary>Window capture via Windows Graphics Capture API (Xbox app, PS Remote Play app).</summary>
    WindowCapture,
}

/// <summary>Common interface for all frame sources that feed the CV pipeline.</summary>
public interface IFrameSource : IDisposable
{
    /// <summary>The type of this source.</summary>
    InputSourceType SourceType { get; }

    /// <summary>Human-readable name of this source (e.g. "Game Capture HD60 X" or "Xbox" window).</summary>
    string SourceName { get; }

    /// <summary>True when the source is actively capturing frames.</summary>
    bool IsCapturing { get; }

    /// <summary>Target frames per second.</summary>
    int TargetFps { get; set; }

    /// <summary>Starts capturing frames. Frames are delivered via the FrameReady event.</summary>
    Task StartAsync();

    /// <summary>Stops capturing.</summary>
    Task StopAsync();

    /// <summary>Raised when a new JPEG frame is ready. Args: (sessionId, jpegBytes).</summary>
    event Action<int, byte[]>? FrameReady;
}
