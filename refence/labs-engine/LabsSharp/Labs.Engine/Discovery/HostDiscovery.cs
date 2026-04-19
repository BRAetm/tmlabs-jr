using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Labs.Engine.Discovery;

/// <summary>
/// Broadcasts the PS5 discovery handshake on UDP/9302 and listens for replies.
/// Pure managed — doesn't require the native DLL. A managed implementation is
/// simpler here than threading through libcurl/libevent, and the PS5 discovery
/// protocol is small and stable.
/// </summary>
public sealed class HostDiscovery : IDisposable
{
    private const int Ps5Port = 9302;
    private const string SrchPs5 = "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:00030010\n";

    private readonly UdpClient _udp;
    private readonly CancellationTokenSource _cts = new();
    private readonly ConcurrentDictionary<IPAddress, DiscoveredHost> _hosts = new();

    public event Action<DiscoveredHost>? HostFound;

    public HostDiscovery()
    {
        _udp = new UdpClient(AddressFamily.InterNetwork)
        {
            EnableBroadcast = true,
        };
        _udp.Client.Bind(new IPEndPoint(IPAddress.Any, 0));
    }

    public IReadOnlyCollection<DiscoveredHost> Hosts => (IReadOnlyCollection<DiscoveredHost>)_hosts.Values;

    public Task StartAsync()
    {
        var listenTask = Task.Run(() => ListenLoop(_cts.Token));
        var broadcastTask = Task.Run(() => BroadcastLoop(_cts.Token));
        return Task.WhenAll(listenTask, broadcastTask);
    }

    private async Task BroadcastLoop(CancellationToken ct)
    {
        var payload = Encoding.ASCII.GetBytes(SrchPs5);
        while (!ct.IsCancellationRequested)
        {
            foreach (var bcast in EnumerateBroadcastAddresses())
            {
                try
                {
                    await _udp.SendAsync(payload, payload.Length, new IPEndPoint(bcast, Ps5Port));
                }
                catch (SocketException) { /* ignore per-iface failures */ }
            }
            try { await Task.Delay(TimeSpan.FromSeconds(2), ct); }
            catch (OperationCanceledException) { break; }
        }
    }

    private async Task ListenLoop(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            UdpReceiveResult r;
            try { r = await _udp.ReceiveAsync(ct); }
            catch (OperationCanceledException) { break; }
            catch (SocketException) { continue; }

            var text = Encoding.ASCII.GetString(r.Buffer);
            if (!text.StartsWith("HTTP/1.1 200", StringComparison.Ordinal)) continue;

            var host = ParseReply(r.RemoteEndPoint.Address, text);
            if (host != null && _hosts.TryAdd(host.Address, host))
                HostFound?.Invoke(host);
        }
    }

    private static DiscoveredHost? ParseReply(IPAddress addr, string body)
    {
        string? name = null, id = null, state = null;
        foreach (var raw in body.Split('\n'))
        {
            var line = raw.Trim();
            var i = line.IndexOf(':');
            if (i <= 0) continue;
            var key = line[..i].Trim().ToLowerInvariant();
            var val = line[(i + 1)..].Trim();
            switch (key)
            {
                case "host-name": name = val; break;
                case "host-id": id = val; break;
                case "host-type": break;
                case "host-request-port": break;
                case "running-app-name": break;
                case "host-status": state = val; break;
            }
        }
        return new DiscoveredHost(addr, name ?? "PS5", id ?? "", state ?? "unknown");
    }

    private static IEnumerable<IPAddress> EnumerateBroadcastAddresses()
    {
        foreach (var nic in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (nic.OperationalStatus != OperationalStatus.Up) continue;
            foreach (var ua in nic.GetIPProperties().UnicastAddresses)
            {
                if (ua.Address.AddressFamily != AddressFamily.InterNetwork) continue;
                if (IPAddress.IsLoopback(ua.Address)) continue;
                var bytes = ua.Address.GetAddressBytes();
                var mask = ua.IPv4Mask?.GetAddressBytes();
                if (mask == null) continue;
                for (int i = 0; i < bytes.Length; i++) bytes[i] = (byte)(bytes[i] | ~mask[i]);
                yield return new IPAddress(bytes);
            }
        }
        yield return IPAddress.Broadcast;
    }

    public void Dispose()
    {
        _cts.Cancel();
        _udp.Dispose();
    }
}

public sealed record DiscoveredHost(IPAddress Address, string Name, string HostId, string Status);
