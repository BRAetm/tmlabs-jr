using System;
using System.Runtime.InteropServices;
using NAudio.Wave;
using Concentus.Structs;
using Labs.Native;

namespace Labs.Engine.Session;

/// <summary>
/// The native labs engine's session-level audio_sink.frame_cb delivers RAW
/// Opus packets (see audioreceiver.c). We decode them in C# with Concentus
/// (pure-managed Opus), then push int16 PCM into a WaveOutEvent.
/// </summary>
public sealed class AudioPlayer : IDisposable
{
    private readonly LabsNative.LabsAudioSinkHeader _headerCb;
    private readonly LabsNative.LabsAudioSinkFrame _frameCb;
    private readonly GCHandle _hH;
    private readonly GCHandle _hF;

    private WaveOutEvent? _out;
    private BufferedWaveProvider? _buf;
    private OpusDecoder? _opus;
    private short[]? _pcm;
    private int _channels;
    private int _rate;
    private int _frameSize;
    private bool _muted;
    private readonly object _gate = new();

    /// <summary>Mute or unmute the output. Silences the WaveOutEvent by setting Volume to 0.</summary>
    public void SetMuted(bool muted)
    {
        lock (_gate)
        {
            _muted = muted;
            if (_out != null)
            {
                try { _out.Volume = muted ? 0f : 1f; } catch { }
            }
            if (muted)
            {
                try { _buf?.ClearBuffer(); } catch { }
            }
        }
    }

    public bool IsMuted
    {
        get { lock (_gate) return _muted; }
    }

    public LabsNative.LabsAudioSink Sink;

    private static readonly string LogPath = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "PSRemotePlay", "labs.log");
    private static void L(string m) { try { System.IO.File.AppendAllText(LogPath, $"[{DateTime.Now:HH:mm:ss.fff}] [audio] {m}\n"); } catch { } }
    private int _fc;

    public AudioPlayer()
    {
        _headerCb = OnHeader;
        _frameCb = OnFrame;
        _hH = GCHandle.Alloc(_headerCb);
        _hF = GCHandle.Alloc(_frameCb);
        Sink = new LabsNative.LabsAudioSink
        {
            user = IntPtr.Zero,
            header_cb = Marshal.GetFunctionPointerForDelegate(_headerCb),
            frame_cb = Marshal.GetFunctionPointerForDelegate(_frameCb),
        };
    }

    private void OnHeader(IntPtr headerPtr, IntPtr user)
    {
        try
        {
            var h = Marshal.PtrToStructure<LabsNative.LabsAudioHeader>(headerPtr);
            lock (_gate)
            {
                try { _out?.Stop(); _out?.Dispose(); } catch { }
                _channels = h.channels;
                _rate = (int)h.rate;
                _frameSize = (int)h.frame_size;

                var fmt = new WaveFormat(_rate, h.bits, _channels);
                _buf = new BufferedWaveProvider(fmt)
                {
                    BufferDuration = TimeSpan.FromMilliseconds(500),
                    DiscardOnBufferOverflow = true,
                    ReadFully = true,
                };
                _out = new WaveOutEvent { DesiredLatency = 100, NumberOfBuffers = 3 };
                _out.Init(_buf);
                try { _out.Volume = _muted ? 0f : 1f; } catch { }
                _out.Play();

                // Opus decoder — 48 kHz is what PS5 uses; Concentus only
                // supports 8/12/16/24/48 kHz, stereo or mono.
                _opus = new OpusDecoder(_rate, _channels);
                _pcm = new short[_frameSize * _channels * 2];
                L($"header · rate={_rate} ch={_channels} bits={h.bits} frame_size={_frameSize}");
            }
        }
        catch { /* audio is non-fatal */ }
    }

    private void OnFrame(IntPtr buf, UIntPtr size, IntPtr user)
    {
        try
        {
            var len = (int)size.ToUInt32();
            if (len <= 0) return;
            var provider = _buf;
            var dec = _opus;
            var pcm = _pcm;
            if (provider == null || dec == null || pcm == null) return;
            if (_muted) return; // hard mute — don't enqueue decoded samples

            // Copy raw Opus packet out of native memory.
            var packet = new byte[len];
            Marshal.Copy(buf, packet, 0, len);

            int decoded;
            try
            {
                decoded = dec.Decode(packet, 0, len, pcm, 0, _frameSize, false);
            }
            catch (Exception ex) { if (_fc < 3) L($"decode ex on #{_fc} len={len}: {ex.Message}"); _fc++; return; }

            if (_fc < 3)
            {
                var head = new System.Text.StringBuilder();
                for (int i = 0; i < Math.Min(8, decoded * _channels); i++) head.Append(pcm[i]).Append(' ');
                L($"decode #{_fc} · pkt={len}B → {decoded} samp/ch · pcm=[{head}]");
            }
            _fc++;
            if (decoded <= 0) return;

            int pcmBytes = decoded * _channels * sizeof(short);
            var outBuf = new byte[pcmBytes];
            Buffer.BlockCopy(pcm, 0, outBuf, 0, pcmBytes);
            provider.AddSamples(outBuf, 0, pcmBytes);
        }
        catch { }
    }

    public void Dispose()
    {
        lock (_gate)
        {
            try { _out?.Stop(); _out?.Dispose(); } catch { }
            _out = null; _buf = null; _opus = null; _pcm = null;
        }
        if (_hH.IsAllocated) _hH.Free();
        if (_hF.IsAllocated) _hF.Free();
    }
}
