using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using NetMQ;
using NetMQ.Sockets;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Labs.Engine.Core;

/// <summary>
/// Receives gamepad state from Python scripts via ZeroMQ SUB socket (port 5556)
/// and relays it to the browser at 60fps via CdpGamepadInjector.SendInput.
/// </summary>
public class GamepadInputRelay : IDisposable
{
    private const int SubPort         = 5590;
    private const int InputIntervalMs = 16; // ~60fps

    private readonly IGamepadSink _gamepadSink;
    private readonly Dictionary<int, GamepadState> _states = new();
    private readonly Dictionary<int, Timer> _timers = new();
    private readonly object _lock = new();

    private SubscriberSocket? _sub;
    private CancellationTokenSource? _cts;
    private Task? _receiveLoop;
    private bool _disposed;

    /// <summary>Creates a new relay wired to the given gamepad sink.</summary>
    public GamepadInputRelay(IGamepadSink gamepadSink)
    {
        _gamepadSink = gamepadSink;
    }

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------

    /// <summary>Binds the ZMQ SUB socket and starts the background receive loop.</summary>
    public void Start()
    {
        _cts = new CancellationTokenSource();
        _sub = new SubscriberSocket();
        _sub.Bind($"tcp://127.0.0.1:{SubPort}");
        _sub.SubscribeToAnyTopic();
        _receiveLoop = Task.Run(() => ReceiveLoopAsync(_cts.Token));
        Console.WriteLine($"[GamepadInputRelay] SUB socket bound on port {SubPort}.");
    }

    /// <summary>Registers a session and starts its 60fps input timer.</summary>
    public void StartSession(int sessionId)
    {
        lock (_lock)
        {
            if (_timers.ContainsKey(sessionId)) return;

            var state = new GamepadState();
            _states[sessionId] = state;
            _timers[sessionId] = new Timer(_ => OnInputTick(sessionId), null, 0, InputIntervalMs);
        }
        Console.WriteLine($"[GamepadInputRelay] Session {sessionId}: 60fps input timer started.");
    }

    /// <summary>Stops the input timer and removes state for a session.</summary>
    public void StopSession(int sessionId)
    {
        lock (_lock)
        {
            if (_timers.Remove(sessionId, out var timer))
                timer.Dispose();
            _states.Remove(sessionId);
        }
        Console.WriteLine($"[GamepadInputRelay] Session {sessionId}: input timer stopped.");
    }

    /// <summary>Returns the current gamepad state for a session, or null if not tracked.</summary>
    public GamepadState? GetState(int sessionId)
    {
        lock (_lock)
            return _states.TryGetValue(sessionId, out var s) ? s : null;
    }

    /// <summary>Stops all timers, closes the SUB socket, and releases resources.</summary>
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        _cts?.Cancel();
        try { _receiveLoop?.Wait(TimeSpan.FromSeconds(2)); } catch { }

        lock (_lock)
        {
            foreach (var t in _timers.Values) t.Dispose();
            _timers.Clear();
            _states.Clear();
        }

        _sub?.Dispose();
        _cts?.Dispose();
        Console.WriteLine("[GamepadInputRelay] Disposed.");
    }

    // ---------------------------------------------------------------------------
    // 60fps timer callback
    // ---------------------------------------------------------------------------

    private void OnInputTick(int sessionId)
    {
        GamepadState? state;
        lock (_lock)
        {
            if (!_states.TryGetValue(sessionId, out state)) return;
        }

        // Only send if session is connected
        if (!_gamepadSink.IsConnected(sessionId)) return;

        // WebView2 must be accessed from the UI thread
        System.Windows.Application.Current?.Dispatcher?.BeginInvoke(() =>
        {
            try { _gamepadSink.SendInput(sessionId, state); } catch { }
        });
    }

    // ---------------------------------------------------------------------------
    // ZMQ receive loop
    // ---------------------------------------------------------------------------

    private async Task ReceiveLoopAsync(CancellationToken ct)
    {
        Console.WriteLine("[GamepadInputRelay] Receive loop started.");

        while (!ct.IsCancellationRequested)
        {
            try
            {
                if (_sub!.TryReceiveFrameString(TimeSpan.FromMilliseconds(100), out var json) && json is not null)
                {
                    ApplyMessage(json);
                }
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex)
            {
                Console.WriteLine($"[GamepadInputRelay] Receive error: {ex.Message}");
            }

            await Task.Yield();
        }

        Console.WriteLine("[GamepadInputRelay] Receive loop stopped.");
    }

    /// <summary>Deserializes a JSON message and applies it to the matching session's GamepadState.</summary>
    private void ApplyMessage(string json)
    {
        try
        {
            var obj = JObject.Parse(json);
            int sessionId = obj["session_id"]?.Value<int>() ?? -1;
            if (sessionId < 0) return;

            lock (_lock)
            {
                if (!_states.TryGetValue(sessionId, out var state)) return;

                // Update axes
                var axes = obj["axes"] as JArray;
                if (axes is not null)
                {
                    for (int i = 0; i < Math.Min(axes.Count, 4); i++)
                        state.Axes[i] = axes[i]?.Value<float>() ?? 0f;
                }

                // Update buttons
                var buttons = obj["buttons"] as JArray;
                if (buttons is not null)
                {
                    for (int i = 0; i < Math.Min(buttons.Count, 17); i++)
                        state.Buttons[i] = buttons[i]?.Value<bool>() ?? false;
                }
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[GamepadInputRelay] Parse failed: {ex.Message}");
        }
    }
}
