using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Net.WebSockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Labs.Engine.Core;

/// <summary>
/// Injects a virtual Xbox gamepad into Xbox Cloud Gaming browser tabs via
/// the Chrome DevTools Protocol (CDP) WebSocket interface.
/// </summary>
public class CdpGamepadInjector : IGamepadSink, IAsyncDisposable
{
    // ---------------------------------------------------------------------------
    // Chrome window size constants (configurable)
    // ---------------------------------------------------------------------------
    private const int ChromeWidth  = 500;
    private const int ChromeHeight = 355;

    // JS injected once per session to install the fake gamepad + visibility overrides
    private const string InjectGamepadJs = @"(function() {
  // --- Virtual gamepad (injected at index 0, real controllers shift to 1+) ---
  const gp = {
    axes: [0,0,0,0],
    buttons: Array(17).fill(null).map(()=>({pressed:false,value:0})),
    connected: true,
    id: 'Xbox 360 Controller (XInput STANDARD GAMEPAD)',
    index: 0,
    mapping: 'standard',
    timestamp: performance.now()
  };
  var _origGetGamepads = navigator.getGamepads.bind(navigator);
  Object.defineProperty(navigator, 'getGamepads', {
    value: function() {
      var real = [];
      try { real = Array.from(_origGetGamepads()); } catch(e) {}
      // Slot 0 = virtual pad, slots 1+ = real controllers (re-indexed)
      var merged = [gp];
      for (var i = 0; i < real.length; i++) {
        if (real[i]) {
          var r = real[i];
          // Clone with updated index so Web Gamepad API stays consistent
          merged.push({
            axes: r.axes, buttons: r.buttons, connected: r.connected,
            id: r.id, index: merged.length, mapping: r.mapping, timestamp: r.timestamp
          });
        }
      }
      // Pad to 4 slots
      while (merged.length < 4) merged.push(null);
      return merged;
    },
    writable: true
  });
  window.__cvGamepad = gp;
  window.dispatchEvent(new Event('gamepadconnected', {bubbles:true}));

  // --- Visibility overrides (keep session alive when backgrounded) ---
  Object.defineProperty(document, 'hidden', { get: () => false, configurable: true });
  Object.defineProperty(document, 'visibilityState', { get: () => 'visible', configurable: true });
  window.addEventListener('visibilitychange', function(e) { e.stopImmediatePropagation(); }, true);

  // --- Heartbeat: re-dispatch gamepadconnected every 25s to keep Xbox Cloud alive ---
  setInterval(function() {
    if (window.__cvGamepad) {
      window.__cvGamepad.timestamp = performance.now();
      window.dispatchEvent(new Event('gamepadconnected', {bubbles:true}));
    }
  }, 25000);
})();";

    private const int PollIntervalMs = 2000;  // poll every 2 seconds
    private const int PollTimeoutMs  = 60_000; // give up after 60 seconds

    private readonly HttpClient _http = new() { Timeout = TimeSpan.FromSeconds(5) };
    private readonly Dictionary<int, CdpConnection> _connections = new();
    private readonly object _connectionsLock = new();

    /// <summary>Fired when a screencast frame arrives from CDP. (sessionId, jpegBytes)</summary>
    public event Action<int, byte[]>? ScreencastFrameReady;

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------

    /// <summary>
    /// Polls the CDP endpoint every 2 seconds until an xbox.com/play tab is found,
    /// then connects via WebSocket and injects the fake gamepad.
    /// Sets session status to Connected on success.
    /// </summary>
    public async Task ConnectAsync(int sessionId)
    {
        int port      = ChromeLauncher.GetDebugPort(sessionId);
        var targetUrl = await PollForXboxCloudTabAsync(port);

        if (targetUrl is null)
            throw new InvalidOperationException(
                $"[CdpGamepadInjector] Session {sessionId}: xbox.com/play tab not found on port {port} after {PollTimeoutMs / 1000}s.");

        var socket = new ClientWebSocket();
        await socket.ConnectAsync(new Uri(targetUrl), CancellationToken.None);
        Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: WebSocket connected → {targetUrl}");

        var cts  = new CancellationTokenSource();
        var conn = new CdpConnection(socket, new SemaphoreSlim(1, 1), cts, msgId: 1);

        lock (_connectionsLock)
            _connections[sessionId] = conn;

        // Run setup commands BEFORE starting the drain loop so responses aren't consumed
        // Enable focus emulation so page thinks it's focused even when backgrounded
        try
        {
            await SendCdpCommandAsync(conn, "Emulation.setFocusEmulationEnabled", new { enabled = true });
            Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: focus emulation enabled.");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: focus emulation failed (non-fatal): {ex.Message}");
        }

        // Register JS to survive page navigations/reloads
        try
        {
            await SendCdpCommandAsync(conn, "Page.addScriptToEvaluateOnNewDocument", new { source = InjectGamepadJs });
            Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: addScriptToEvaluateOnNewDocument registered.");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: addScriptToEvaluateOnNewDocument failed (non-fatal): {ex.Message}");
        }

        // Lock Chrome window to target size via CDP
        await SetWindowSizeAsync(sessionId);

        // Inject the fake gamepad + visibility overrides into current page (fire-and-forget OK)
        await EvaluateAsync(conn, InjectGamepadJs);
        Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: gamepad + visibility overrides injected.");

        // NOW start the receive loop — handles screencast frames + drains other messages
        conn.ReceiveLoop = Task.Run(() => ReceiveLoopAsync(sessionId, conn), cts.Token);
    }

    /// <summary>
    /// Sends a gamepad state update to the injected Xbox gamepad in the browser tab.
    /// Called at 30fps from EventReceived — fire-and-forget; dropped frames are acceptable.
    /// </summary>
    public void UpdateGamepadState(int sessionId, GamepadEvent evt)
    {
        CdpConnection? conn;
        lock (_connectionsLock)
            _connections.TryGetValue(sessionId, out conn);

        if (conn is null) return;

        _ = SendUpdateAsync(conn, evt);
    }

    /// <summary>
    /// Sends a GamepadState (array-based Web Gamepad API format) to the browser.
    /// Called at 60fps from GamepadInputRelay — fire-and-forget; dropped frames are acceptable.
    /// </summary>
    public void SendInput(int sessionId, GamepadState state)
    {
        CdpConnection? conn;
        lock (_connectionsLock)
            _connections.TryGetValue(sessionId, out conn);

        if (conn is null) return;

        _ = SendGamepadStateAsync(conn, state);
    }

    /// <summary>Returns true if a CDP connection exists for the given session.</summary>
    public bool IsConnected(int sessionId)
    {
        lock (_connectionsLock)
            return _connections.ContainsKey(sessionId);
    }

    /// <summary>
    /// Starts streaming game frames from Chrome via CDP Page.startScreencast.
    /// Frames arrive as base64 JPEG via the WebSocket and fire ScreencastFrameReady.
    /// </summary>
    public async Task StartScreencastAsync(int sessionId, int maxFps = 30, int quality = 80)
    {
        CdpConnection? conn;
        lock (_connectionsLock)
            _connections.TryGetValue(sessionId, out conn);

        if (conn is null || conn.Socket.State != WebSocketState.Open)
        {
            Console.WriteLine($"[CdpScreencast] Session {sessionId}: no active CDP connection.");
            return;
        }

        try
        {
            // Enable Page domain (required for screencast)
            await SendCdpCommandAsync(conn, "Page.enable", new { });

            await SendCdpCommandAsync(conn, "Page.startScreencast", new
            {
                format = "jpeg",
                quality,
                maxWidth = 960,
                maxHeight = 540,
                everyNthFrame = Math.Max(1, 60 / maxFps) // e.g. 2 = every other frame = 30fps
            });

            conn.ScreencastActive = true;
            Console.WriteLine($"[CdpScreencast] Session {sessionId}: screencast started (quality={quality}, maxFps≈{maxFps})");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[CdpScreencast] Session {sessionId}: start failed — {ex.Message}");
        }
    }

    /// <summary>Stops the CDP screencast.</summary>
    public async Task StopScreencastAsync(int sessionId)
    {
        CdpConnection? conn;
        lock (_connectionsLock)
            _connections.TryGetValue(sessionId, out conn);

        if (conn is null) return;

        try
        {
            conn.ScreencastActive = false;
            if (conn.Socket.State == WebSocketState.Open)
                await SendCdpCommandAsync(conn, "Page.stopScreencast", new { });
            Console.WriteLine($"[CdpScreencast] Session {sessionId}: screencast stopped.");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[CdpScreencast] Session {sessionId}: stop failed — {ex.Message}");
        }
    }

    /// <summary>Disconnects the CDP session and cleans up resources.</summary>
    public async Task DisconnectAsync(int sessionId)
    {
        CdpConnection? conn;
        lock (_connectionsLock)
        {
            _connections.TryGetValue(sessionId, out conn);
            _connections.Remove(sessionId);
        }

        if (conn is null) return;
        await ShutdownConnectionAsync(sessionId, conn);
    }

    /// <summary>Disconnects all active sessions.</summary>
    public async ValueTask DisposeAsync()
    {
        List<(int id, CdpConnection conn)> all;
        lock (_connectionsLock)
        {
            all = new List<(int, CdpConnection)>();
            foreach (var kv in _connections)
                all.Add((kv.Key, kv.Value));
            _connections.Clear();
        }

        foreach (var (id, conn) in all)
            await ShutdownConnectionAsync(id, conn);

        _http.Dispose();
    }

    // ---------------------------------------------------------------------------
    // CDP target discovery — polls every 2 seconds for xbox.com/play tab
    // ---------------------------------------------------------------------------

    /// <summary>Polls /json every 2 seconds until the xbox.com/play tab appears or timeout.</summary>
    private async Task<string?> PollForXboxCloudTabAsync(int port)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(PollTimeoutMs);
        int attempt  = 0;

        while (DateTime.UtcNow < deadline)
        {
            attempt++;
            try
            {
                var json    = await _http.GetStringAsync($"http://localhost:{port}/json");
                var targets = JArray.Parse(json);

                foreach (var target in targets)
                {
                    var type  = target["type"]?.ToString();
                    var url   = target["url"]?.ToString() ?? string.Empty;
                    var title = target["title"]?.ToString() ?? string.Empty;
                    var ws    = target["webSocketDebuggerUrl"]?.ToString();

                    if (type == "page" && ws is not null &&
                        (url.Contains("xbox.com/play", StringComparison.OrdinalIgnoreCase) ||
                         url.Contains("xcloud",        StringComparison.OrdinalIgnoreCase) ||
                         title.Contains("Xbox Cloud",  StringComparison.OrdinalIgnoreCase)))
                    {
                        Console.WriteLine($"[CdpGamepadInjector] Found xbox.com/play tab on attempt {attempt}: {url}");
                        return ws;
                    }
                }

                Console.WriteLine($"[CdpGamepadInjector] Poll {attempt}: no xbox.com/play tab yet on port {port}.");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[CdpGamepadInjector] Poll {attempt} failed: {ex.Message}");
            }

            await Task.Delay(PollIntervalMs);
        }

        return null;
    }

    // ---------------------------------------------------------------------------
    // CDP messaging
    // ---------------------------------------------------------------------------

    /// <summary>Sends a Runtime.evaluate CDP command and awaits the send (not the JS result).</summary>
    private async Task EvaluateAsync(CdpConnection conn, string expression)
    {
        int id       = Interlocked.Increment(ref conn.NextMsgId);
        var payload  = new
        {
            id,
            method = "Runtime.evaluate",
            @params = new { expression, returnByValue = false, awaitPromise = false }
        };

        var json  = JsonConvert.SerializeObject(payload);
        var bytes = Encoding.UTF8.GetBytes(json);

        await conn.SendLock.WaitAsync();
        try
        {
            await conn.Socket.SendAsync(
                new ArraySegment<byte>(bytes),
                WebSocketMessageType.Text,
                endOfMessage: true,
                CancellationToken.None);
        }
        finally
        {
            conn.SendLock.Release();
        }
    }

    /// <summary>Builds and sends a JS gamepad state update. Drops the frame if a send is already in flight.</summary>
    private async Task SendUpdateAsync(CdpConnection conn, GamepadEvent evt)
    {
        if (!await conn.SendLock.WaitAsync(0)) return; // skip frame if busy
        try
        {
            if (conn.Socket.State != WebSocketState.Open) return;

            int id       = Interlocked.Increment(ref conn.NextMsgId);
            var js       = BuildUpdateJs(evt);
            var payload  = new
            {
                id,
                method = "Runtime.evaluate",
                @params = new { expression = js, returnByValue = false, awaitPromise = false }
            };

            var bytes = Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(payload));
            await conn.Socket.SendAsync(
                new ArraySegment<byte>(bytes),
                WebSocketMessageType.Text,
                endOfMessage: true,
                CancellationToken.None);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[CdpGamepadInjector] SendUpdate failed: {ex.Message}");
        }
        finally
        {
            conn.SendLock.Release();
        }
    }

    /// <summary>Sends a GamepadState update via CDP. Drops the frame if a send is already in flight.</summary>
    private async Task SendGamepadStateAsync(CdpConnection conn, GamepadState state)
    {
        if (!await conn.SendLock.WaitAsync(0)) return;
        try
        {
            if (conn.Socket.State != WebSocketState.Open) return;

            int id      = Interlocked.Increment(ref conn.NextMsgId);
            var js      = BuildGamepadStateJs(state);
            var payload = new
            {
                id,
                method = "Runtime.evaluate",
                @params = new { expression = js, returnByValue = false, awaitPromise = false }
            };

            var bytes = Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(payload));
            await conn.Socket.SendAsync(
                new ArraySegment<byte>(bytes),
                WebSocketMessageType.Text,
                endOfMessage: true,
                CancellationToken.None);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[CdpGamepadInjector] SendInput failed: {ex.Message}");
        }
        finally
        {
            conn.SendLock.Release();
        }
    }

    /// <summary>Builds compact JS that updates window.__cvGamepad from a GamepadState.</summary>
    private static string BuildGamepadStateJs(GamepadState state)
    {
        static string F(float v) => v.ToString("F4", System.Globalization.CultureInfo.InvariantCulture);
        static string B(bool v)  => v ? "true" : "false";

        var sb = new StringBuilder(512);
        sb.Append("(function(){var g=window.__cvGamepad;if(!g)return;");

        for (int i = 0; i < 4; i++)
            sb.Append($"g.axes[{i}]={F(state.Axes[i])};");

        for (int i = 0; i < 17; i++)
        {
            sb.Append($"g.buttons[{i}].pressed={B(state.Buttons[i])};");
            sb.Append($"g.buttons[{i}].value={B(state.Buttons[i])}|0;");
        }

        sb.Append("g.timestamp=performance.now();})();");
        return sb.ToString();
    }

    /// <summary>
    /// Receives and processes CDP messages. Handles Page.screencastFrame events
    /// (decodes base64 JPEG, fires ScreencastFrameReady, sends ack).
    /// All other messages are drained/discarded.
    /// </summary>
    private async Task ReceiveLoopAsync(int sessionId, CdpConnection conn)
    {
        // Large buffer to handle screencast frames (base64 JPEG can be 100KB+)
        var buffer = new byte[512_000];
        int frameCount = 0;

        try
        {
            while (conn.Socket.State == WebSocketState.Open &&
                   !conn.Cts.Token.IsCancellationRequested)
            {
                // Read full message (may span multiple fragments)
                int totalBytes = 0;
                WebSocketReceiveResult result;
                do
                {
                    result = await conn.Socket.ReceiveAsync(
                        new ArraySegment<byte>(buffer, totalBytes, buffer.Length - totalBytes),
                        conn.Cts.Token);
                    totalBytes += result.Count;
                } while (!result.EndOfMessage && totalBytes < buffer.Length);

                if (result.MessageType != WebSocketMessageType.Text || totalBytes == 0)
                    continue;

                // Fast check for screencast frame before full JSON parse
                var msgSpan = Encoding.UTF8.GetString(buffer, 0, totalBytes);

                if (conn.ScreencastActive && msgSpan.Contains("Page.screencastFrame"))
                {
                    try
                    {
                        var msg = JObject.Parse(msgSpan);
                        var method = msg["method"]?.ToString();

                        if (method == "Page.screencastFrame")
                        {
                            var parms = msg["params"];
                            var data = parms?["data"]?.ToString();        // base64 JPEG
                            var sessId = parms?["sessionId"]?.Value<int>() ?? 0;

                            if (data is not null)
                            {
                                var jpegBytes = Convert.FromBase64String(data);
                                frameCount++;

                                if (frameCount <= 3 || frameCount % 100 == 0)
                                    Console.WriteLine($"[CdpScreencast] Session {sessionId}: frame #{frameCount} ({jpegBytes.Length} bytes)");

                                // Fire event so FramePublisher can send to Python
                                ScreencastFrameReady?.Invoke(sessionId, jpegBytes);
                            }

                            // ACK the frame so Chrome sends the next one
                            _ = AckScreencastFrameAsync(conn, sessId);
                        }
                    }
                    catch (Exception ex)
                    {
                        if (frameCount <= 5)
                            Console.WriteLine($"[CdpScreencast] Session {sessionId}: parse error — {ex.Message}");
                    }
                }
                // All other messages: drain (ignore)
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: receive loop ended — {ex.Message}");
        }

        Console.WriteLine($"[CdpScreencast] Session {sessionId}: receive loop exited ({frameCount} screencast frames total)");
    }

    /// <summary>Sends Page.screencastFrameAck so Chrome continues sending frames.</summary>
    private async Task AckScreencastFrameAsync(CdpConnection conn, int frameSessionId)
    {
        if (!await conn.SendLock.WaitAsync(0)) return; // skip if busy
        try
        {
            if (conn.Socket.State != WebSocketState.Open) return;

            int id = Interlocked.Increment(ref conn.NextMsgId);
            var payload = new
            {
                id,
                method = "Page.screencastFrameAck",
                @params = new { sessionId = frameSessionId }
            };

            var bytes = Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(payload));
            await conn.Socket.SendAsync(
                new ArraySegment<byte>(bytes),
                WebSocketMessageType.Text,
                endOfMessage: true,
                CancellationToken.None);
        }
        catch { }
        finally
        {
            conn.SendLock.Release();
        }
    }

    private static async Task ShutdownConnectionAsync(int sessionId, CdpConnection conn)
    {
        conn.Cts.Cancel();

        try { await conn.ReceiveLoop.ConfigureAwait(false); }
        catch { /* already cancelled */ }

        try
        {
            if (conn.Socket.State == WebSocketState.Open)
                await conn.Socket.CloseAsync(WebSocketCloseStatus.NormalClosure, "session closed", CancellationToken.None);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: close failed — {ex.Message}");
        }

        conn.Socket.Dispose();
        conn.SendLock.Dispose();
        conn.Cts.Dispose();
        Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: disconnected.");
    }

    // ---------------------------------------------------------------------------
    // JS builder
    // ---------------------------------------------------------------------------

    /// <summary>Builds a compact JS expression that updates window.__cvGamepad to match the GamepadEvent.</summary>
    private static string BuildUpdateJs(GamepadEvent evt)
    {
        var b = evt.Buttons;

        bool btnA    = (b & 0x1000) != 0;
        bool btnB    = (b & 0x2000) != 0;
        bool btnX    = (b & 0x4000) != 0;
        bool btnY    = (b & 0x8000) != 0;
        bool btnLb   = (b & 0x0100) != 0;
        bool btnRb   = (b & 0x0200) != 0;
        bool btnBack = (b & 0x0020) != 0;
        bool btnStart= (b & 0x0010) != 0;
        bool dUp     = (b & 0x0001) != 0;
        bool dDown   = (b & 0x0002) != 0;
        bool dLeft   = (b & 0x0004) != 0;
        bool dRight  = (b & 0x0008) != 0;

        float lt = Math.Clamp(evt.LeftTrigger,  0f, 1f);
        float rt = Math.Clamp(evt.RightTrigger, 0f, 1f);

        static string Js(bool v)  => v ? "true"  : "false";
        static string Flt(float v) => v.ToString("F4", System.Globalization.CultureInfo.InvariantCulture);

        return
            "(function(){var g=window.__cvGamepad;if(!g)return;" +
            $"g.axes[0]={Flt(evt.LeftStickX)};" +
            $"g.axes[1]={Flt(evt.LeftStickY)};" +
            $"g.axes[2]={Flt(evt.RightStickX)};" +
            $"g.axes[3]={Flt(evt.RightStickY)};" +
            // Buttons 0–7
            $"g.buttons[0].pressed={Js(btnA)};g.buttons[0].value={Js(btnA)}|0;" +
            $"g.buttons[1].pressed={Js(btnB)};g.buttons[1].value={Js(btnB)}|0;" +
            $"g.buttons[2].pressed={Js(btnX)};g.buttons[2].value={Js(btnX)}|0;" +
            $"g.buttons[3].pressed={Js(btnY)};g.buttons[3].value={Js(btnY)}|0;" +
            $"g.buttons[4].pressed={Js(btnLb)};g.buttons[4].value={Js(btnLb)}|0;" +
            $"g.buttons[5].pressed={Js(btnRb)};g.buttons[5].value={Js(btnRb)}|0;" +
            $"g.buttons[6].pressed={Flt(lt)}>0.1;g.buttons[6].value={Flt(lt)};" +
            $"g.buttons[7].pressed={Flt(rt)}>0.1;g.buttons[7].value={Flt(rt)};" +
            // Buttons 8–9
            $"g.buttons[8].pressed={Js(btnBack)};g.buttons[8].value={Js(btnBack)}|0;" +
            $"g.buttons[9].pressed={Js(btnStart)};g.buttons[9].value={Js(btnStart)}|0;" +
            // D-Pad 12–15
            $"g.buttons[12].pressed={Js(dUp)};g.buttons[12].value={Js(dUp)}|0;" +
            $"g.buttons[13].pressed={Js(dDown)};g.buttons[13].value={Js(dDown)}|0;" +
            $"g.buttons[14].pressed={Js(dLeft)};g.buttons[14].value={Js(dLeft)}|0;" +
            $"g.buttons[15].pressed={Js(dRight)};g.buttons[15].value={Js(dRight)}|0;" +
            "g.timestamp=performance.now();})();";
    }

    // ---------------------------------------------------------------------------
    // Chrome window sizing (CDP + Win32 fallback)
    // ---------------------------------------------------------------------------

    /// <summary>Sets Chrome window size via CDP Browser.getWindowForTarget + Browser.setWindowBounds.</summary>
    public async Task SetWindowSizeAsync(int sessionId)
    {
        CdpConnection? conn;
        lock (_connectionsLock)
            _connections.TryGetValue(sessionId, out conn);

        if (conn is null || conn.Socket.State != WebSocketState.Open) return;

        try
        {
            // 1. Get windowId via Browser.getWindowForTarget
            var windowResult = await SendCdpCommandAsync(conn, "Browser.getWindowForTarget", new { });
            if (windowResult is null) return;

            var windowId = windowResult["windowId"]?.Value<int>();
            if (windowId is null)
            {
                Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: windowId not found in response.");
                return;
            }

            // 2. Set bounds via Browser.setWindowBounds
            await SendCdpCommandAsync(conn, "Browser.setWindowBounds", new
            {
                windowId = windowId.Value,
                bounds = new
                {
                    width = ChromeWidth,
                    height = ChromeHeight,
                    windowState = "normal"
                }
            });

            Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: window sized to {ChromeWidth}x{ChromeHeight} via CDP.");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: CDP window sizing failed — {ex.Message}");
        }
    }

    /// <summary>Sets Chrome window size via Win32 SetWindowPos (fallback).</summary>
    public static void SetWindowSizeViaWinApi(nint hwnd)
    {
        if (hwnd == 0) return;

        try
        {
            const uint SWP_NOMOVE   = 0x0002;
            const uint SWP_NOZORDER = 0x0004;
            SetWindowPos(hwnd, IntPtr.Zero, 0, 0, ChromeWidth, ChromeHeight, SWP_NOMOVE | SWP_NOZORDER);
            Console.WriteLine($"[CdpGamepadInjector] Window 0x{hwnd:X} sized to {ChromeWidth}x{ChromeHeight} via Win32.");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[CdpGamepadInjector] Win32 SetWindowPos failed: {ex.Message}");
        }
    }

    /// <summary>Starts a background loop that re-applies window size every 5 seconds.</summary>
    public void StartWindowSizePolling(int sessionId, nint hwnd)
    {
        _ = Task.Run(async () =>
        {
            while (true)
            {
                await Task.Delay(5000);

                CdpConnection? conn;
                lock (_connectionsLock)
                    _connections.TryGetValue(sessionId, out conn);

                // Stop polling if session disconnected
                if (conn is null) break;

                // Try CDP first, fall back to Win32
                try
                {
                    await SetWindowSizeAsync(sessionId);
                }
                catch
                {
                    SetWindowSizeViaWinApi(hwnd);
                }
            }
        });
    }

    /// <summary>Starts a background loop that sends PostMessage(WM_NULL) every 30s to prevent Windows suspension.</summary>
    public void StartKeepAliveHeartbeat(int sessionId, nint hwnd)
    {
        if (hwnd == 0) return;

        _ = Task.Run(async () =>
        {
            Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: PostMessage heartbeat started (HWND 0x{hwnd:X}).");

            while (true)
            {
                await Task.Delay(30_000);

                // Stop if session disconnected
                CdpConnection? conn;
                lock (_connectionsLock)
                    _connections.TryGetValue(sessionId, out conn);

                if (conn is null) break;

                try
                {
                    PostMessage(hwnd, WM_NULL, IntPtr.Zero, IntPtr.Zero);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: PostMessage heartbeat failed: {ex.Message}");
                }
            }

            Console.WriteLine($"[CdpGamepadInjector] Session {sessionId}: PostMessage heartbeat stopped.");
        });
    }

    /// <summary>Sends a CDP command and returns the result object.</summary>
    private async Task<JObject?> SendCdpCommandAsync(CdpConnection conn, string method, object parameters)
    {
        if (conn.Socket.State != WebSocketState.Open) return null;

        int id = Interlocked.Increment(ref conn.NextMsgId);
        var payload = new
        {
            id,
            method,
            @params = parameters
        };

        var json = JsonConvert.SerializeObject(payload);
        var bytes = Encoding.UTF8.GetBytes(json);

        await conn.SendLock.WaitAsync();
        try
        {
            await conn.Socket.SendAsync(
                new ArraySegment<byte>(bytes),
                WebSocketMessageType.Text,
                endOfMessage: true,
                CancellationToken.None);
        }
        finally
        {
            conn.SendLock.Release();
        }

        // Read response (simplified — waits for the matching response)
        var buffer = new byte[8192];
        var deadline = DateTime.UtcNow.AddSeconds(5);
        while (DateTime.UtcNow < deadline)
        {
            var result = await conn.Socket.ReceiveAsync(new ArraySegment<byte>(buffer), CancellationToken.None);
            if (result.MessageType == WebSocketMessageType.Text)
            {
                var responseJson = Encoding.UTF8.GetString(buffer, 0, result.Count);
                var response = JObject.Parse(responseJson);
                if (response["id"]?.Value<int>() == id)
                    return response["result"] as JObject;
            }
        }

        return null;
    }

    // ---------------------------------------------------------------------------
    // P/Invoke for Win32 window sizing fallback
    // ---------------------------------------------------------------------------

    private const uint WM_NULL = 0x0000;

    [DllImport("user32.dll")]
    private static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter,
        int x, int y, int cx, int cy, uint uFlags);

    // ---------------------------------------------------------------------------
    // Connection record
    // ---------------------------------------------------------------------------

    private sealed class CdpConnection
    {
        public readonly ClientWebSocket       Socket;
        public readonly SemaphoreSlim         SendLock;
        public readonly CancellationTokenSource Cts;
        public          int                   NextMsgId;
        public          Task                  ReceiveLoop = Task.CompletedTask;
        public volatile bool                  ScreencastActive;

        public CdpConnection(ClientWebSocket socket, SemaphoreSlim sendLock,
                              CancellationTokenSource cts, int msgId)
        {
            Socket     = socket;
            SendLock   = sendLock;
            Cts        = cts;
            NextMsgId  = msgId;
        }
    }
}
