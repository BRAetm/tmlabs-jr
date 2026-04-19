# Handoff — LabsSharp session fixes (2026-04-19)

## What was changed

### `Labs.RemotePlay.Wpf/MainWindow.xaml.cs`

**`StartXboxRpInline()`**
Switched from `XboxRemotePlayView` (WebView2 embed) to `XboxRpView` (Windows Graphics Capture window picker). Matches the tile description "capture the Xbox desktop app window."

**`EndSession()`**
Removed early-return branch that skipped `SessionHost.Child = null` for non-PS sessions. All session types now fully clear the host on end.

**`UpdateFpsPill()`**
Split `sessionActive` — `StopSessionBtn` still tracks `SessionHost.Child is not null`, but `StatsHud` and `ControllerOverlay` now only appear when `GetActiveDriver() is not null` (i.e., capture is active, not during picker phase). `ControllerOverlay` is additionally suppressed for `XboxRpView` sessions.

**`_xinput.StateChanged`**
Added `XboxRpView` branch: physical XInput state is forwarded to `_currentFanOut.SendInput()` → `CaptureCardSink` → ViGEm virtual pad. The existing `XboxRemotePlayView` branch (WebView2 gamepad shim) is unchanged.

---

### `Labs.Engine/Core/FramePublisher.cs`

`TickFps` renamed to `NotifyFrame` and made `public`. All three internal callers (`PublishFrame`, `PublishRawFrame`, `PublishBgra`) updated.

---

### `Labs.RemotePlay.Wpf/Panels/XboxRpView.xaml.cs`

`OnBitmapReady` now calls `_pub.NotifyFrame(sessionId)` after updating `VideoSurface.Source` so the FPS pill reflects the WGC direct-blit path.

---

### `Labs.RemotePlay.Wpf/Panels/XboxRemotePlayView.xaml.cs`

**FPS tick** — `InitAsync` injects a JS `requestAnimationFrame` loop that posts a WebMessage each frame; `WebMessageReceived` calls `_pub.NotifyFrame(SessionId)`. FPS pill now shows live rate for WebView2 sessions.

**Cleanup** — `Web.Dispose()` added to `Cleanup()` so the WebView2 host and its `msedgewebview2.exe` subprocess close when the session ends.

---

## Current state

| Feature | Status |
|---|---|
| Xbox Remote Play (WGC picker) | working |
| Xbox Remote Play controller injection (ViGEm) | working |
| FPS counter — WGC direct path | working |
| FPS counter — WebView2 sessions | working (rAF tick) |
| Session cleanup / no lingering views | working |
| Controller overlay hidden for Xbox stream | working |

## Open / known

- `XboxRemotePlayView.SessionEnded` event is declared but never fired (CS0067 warning). The view has no internal trigger to end the session — user must use the top-bar "stop session" button.
- ViGEm injection for `XboxRpView` only activates after the user clicks **Start capture** in the picker (driver is null until then).
- `XboxView` (Cloud Gaming WebView2) does not have `Web.Dispose()` in its cleanup — same leak risk as the old `XboxRemotePlayView`. Worth adding.
