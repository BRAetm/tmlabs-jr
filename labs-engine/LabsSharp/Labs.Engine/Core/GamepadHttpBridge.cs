using System;
using System.IO;
using System.Net;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace Labs.Engine.Core;

/// <summary>
/// Localhost HTTP bridge: accepts POST /gamepad with a Web Gamepad-API-shaped
/// payload (<c>{"axes":[lx,ly,rx,ry], "buttons":[bool...]}</c>) and forwards it
/// to whichever <see cref="IGamepadSink"/> is currently active. Lets an external
/// process (labs-labeler's CV script) drive the virtual pad without needing a
/// shared assembly.
/// </summary>
public sealed class GamepadHttpBridge : IDisposable
{
    private readonly HttpListener _listener = new();
    private readonly CancellationTokenSource _cts = new();
    private IGamepadSink? _sink;
    private int _sessionId;
    private Task? _loop;
    private bool _disposed;

    public int Port { get; }

    public GamepadHttpBridge(int port = 8765)
    {
        Port = port;
        _listener.Prefixes.Add($"http://127.0.0.1:{port}/");
    }

    /// <summary>Point the bridge at an active sink. Pass null to detach.</summary>
    public void SetActiveSink(IGamepadSink? sink, int sessionId)
    {
        _sink = sink;
        _sessionId = sessionId;
    }

    public void Start()
    {
        try { _listener.Start(); }
        catch (Exception ex)
        {
            Console.WriteLine($"[GamepadHttpBridge] failed to bind :{Port} — {ex.Message}");
            return;
        }
        _loop = Task.Run(AcceptLoop);
        Console.WriteLine($"[GamepadHttpBridge] listening on http://127.0.0.1:{Port}/");
    }

    private async Task AcceptLoop()
    {
        while (!_cts.IsCancellationRequested && _listener.IsListening)
        {
            HttpListenerContext ctx;
            try { ctx = await _listener.GetContextAsync().ConfigureAwait(false); }
            catch { return; }
            _ = Task.Run(() => HandleRequest(ctx));
        }
    }

    private void HandleRequest(HttpListenerContext ctx)
    {
        try
        {
            var req = ctx.Request;
            var path = req.Url?.AbsolutePath ?? "";

            if (req.HttpMethod == "GET" && path == "/status")
            {
                var body = JsonSerializer.SerializeToUtf8Bytes(new
                {
                    connected = _sink != null,
                    session = _sessionId,
                });
                Reply(ctx, 200, "application/json", body);
                return;
            }

            if (req.HttpMethod != "POST" || path != "/gamepad")
            {
                Reply(ctx, 404, "text/plain", Encoding.UTF8.GetBytes("not found"));
                return;
            }

            using var reader = new StreamReader(req.InputStream, req.ContentEncoding ?? Encoding.UTF8);
            var json = reader.ReadToEnd();

            GamepadState? state;
            try { state = JsonSerializer.Deserialize<GamepadState>(json, new JsonSerializerOptions { PropertyNameCaseInsensitive = true }); }
            catch (JsonException ex)
            {
                Reply(ctx, 400, "text/plain", Encoding.UTF8.GetBytes("bad json: " + ex.Message));
                return;
            }

            var sink = _sink;
            if (state == null || sink == null)
            {
                Reply(ctx, 503, "text/plain", Encoding.UTF8.GetBytes("no active session"));
                return;
            }

            try { sink.SendInput(_sessionId, state); }
            catch (Exception ex)
            {
                Reply(ctx, 500, "text/plain", Encoding.UTF8.GetBytes("sink error: " + ex.Message));
                return;
            }

            Reply(ctx, 204, "text/plain", Array.Empty<byte>());
        }
        catch
        {
            try { ctx.Response.Abort(); } catch { }
        }
    }

    private static void Reply(HttpListenerContext ctx, int status, string contentType, byte[] body)
    {
        try
        {
            ctx.Response.StatusCode = status;
            ctx.Response.ContentType = contentType;
            ctx.Response.ContentLength64 = body.Length;
            if (body.Length > 0) ctx.Response.OutputStream.Write(body, 0, body.Length);
            ctx.Response.Close();
        }
        catch { }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        try { _cts.Cancel(); } catch { }
        try { _listener.Stop(); } catch { }
        try { _listener.Close(); } catch { }
        try { _loop?.Wait(500); } catch { }
    }
}
