using System;
using System.Runtime.InteropServices;
using Labs.Engine.Discovery;
using Labs.Engine.Registration;
using Labs.Native;

namespace Labs.Engine.Session;

/// <summary>
/// Wraps labs_session_* for LAN streaming. Init + start a session, forward
/// events, and expose raw H.264/H.265 samples via <see cref="SampleReady"/>.
///
/// Video decoding is NOT handled here — add a managed decoder (FFmpeg.AutoGen
/// is the recommended route) that consumes SampleReady bytes, decodes to
/// BGRA, and raises <see cref="FrameReady"/>.
/// </summary>
public sealed class StreamSession : IDisposable
{
    private readonly DiscoveredHost _host;
    private readonly RegisteredCredentials _creds;
    private readonly LabsLogger _logger;

    private IntPtr _session;
    private IntPtr _hostStrPtr;
    private GCHandle _eventCbHandle;
    private GCHandle _videoCbHandle;
    private LabsNative.LabsEventCallback? _eventCb;
    private LabsNative.LabsVideoSampleCallback? _videoCb;
    private bool _started;

    public IntPtr NativeSession => _session;
    public event Action<byte[], bool>? SampleReady;       // bytes, frameRecovered

    private VideoDecoder? _decoder;
    private volatile bool _decoderDisposed;
    private AudioPlayer? _audio;
    public bool Hevc { get; set; } = false;
    public bool EnableDecoder { get; set; } = true;

    /// <summary>Mute/unmute the audio output. Safe to call before audio starts.</summary>
    public void SetMuted(bool muted) => _audio?.SetMuted(muted);

    private readonly System.Threading.Channels.Channel<byte[]> _decodeQueue =
        System.Threading.Channels.Channel.CreateBounded<byte[]>(
            new System.Threading.Channels.BoundedChannelOptions(32)
            {
                FullMode = System.Threading.Channels.BoundedChannelFullMode.DropOldest,
                SingleReader = true,
                SingleWriter = true,
            });
    private System.Threading.Thread? _decodeThread;
    public event Action<int, int, byte[], int>? FrameReady; // w, h, bgraBytes, stride — wired by decoder
    public event Action<string>? StatusChanged;
    public event Action<LabsNative.LabsEventType, IntPtr>? NativeEvent;

    private static readonly string LogPath = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "PSRemotePlay", "labs.log");
    private static void L(string msg)
    {
        try { System.IO.File.AppendAllText(LogPath, $"[{DateTime.Now:HH:mm:ss.fff}] {msg}\n"); } catch { }
    }

    public StreamSession(DiscoveredHost host, RegisteredCredentials creds)
    {
        _host = host;
        _creds = creds;
        _logger = new LabsLogger();
        _logger.Message += (lvl, msg) => { L($"native[{lvl}] {msg}"); StatusChanged?.Invoke($"[{lvl}] {msg}"); };
    }

    public void Start()
    {
        L("Start() enter");
        LabsRuntime.Initialize();

        var size = (int)LabsNative.labs_session_sizeof().ToUInt32();
        L($"labs_session_sizeof = {size}");
        _session = Marshal.AllocHGlobal(size);
        unsafe { new Span<byte>((void*)_session, size).Clear(); }

        _hostStrPtr = Marshal.StringToHGlobalAnsi(_host.Address.ToString());

        var info = new LabsNative.LabsConnectInfo
        {
            ps5 = 1,
            host = _hostStrPtr,
            video_profile = new LabsNative.LabsConnectVideoProfile
            {
                width = 1920, height = 1080, max_fps = 60, bitrate = 15000,
                codec = Hevc ? LabsNative.LabsCodec.LABS_CODEC_H265 : LabsNative.LabsCodec.LABS_CODEC_H264,
            },
            video_profile_auto_downgrade = 1,
            enable_keyboard = 0,
            enable_dualsense = 1,
            audio_video_disabled = 0,
            auto_regist = 0,
            rudp_sock = IntPtr.Zero,
            packet_loss_max = 0.05,
            enable_idr_on_fec_failure = 1,
        };
        unsafe
        {
            if (_creds.RpRegistKey.Length != LabsNative.LABS_SESSION_AUTH_SIZE)
                throw new InvalidOperationException("Registered credentials have wrong regist_key size.");
            for (int i = 0; i < LabsNative.LABS_SESSION_AUTH_SIZE; i++) info.regist_key[i] = _creds.RpRegistKey[i];
            // morning[0x10] is derived in native code from rp_key — zero here; native fills it.
            for (int i = 0; i < 16 && i < _creds.RpKey.Length; i++) info.morning[i] = _creds.RpKey[i];
            // psn_account_id not required for LAN with registered host — zero.
        }

        L($"calling labs_session_init (regist_key[0..4]=...{_creds.RpRegistKey[0]:X2}{_creds.RpRegistKey[1]:X2})");
        var err = LabsNative.labs_session_init(_session, ref info, ref _logger.Native);
        L($"labs_session_init -> {err}");
        if (err != LabsNative.LabsErrorCode.LABS_ERR_SUCCESS)
            throw new LabsException("labs_session_init failed", err);

        _eventCb = OnEvent;
        _videoCb = OnVideoSample;
        _eventCbHandle = GCHandle.Alloc(_eventCb);
        _videoCbHandle = GCHandle.Alloc(_videoCb);
        LabsNative.labs_session_set_event_cb(_session, Marshal.GetFunctionPointerForDelegate(_eventCb), IntPtr.Zero);
        LabsNative.labs_session_set_video_sample_cb(_session, Marshal.GetFunctionPointerForDelegate(_videoCb), IntPtr.Zero);

        _audio = new AudioPlayer();
        LabsNative.labs_session_set_audio_sink(_session, ref _audio.Sink);

        if (EnableDecoder)
        {
            // Decoder is COM; create + use it all on the same thread (MTA).
            _decodeThread = new System.Threading.Thread(DecodeLoop)
            {
                IsBackground = true, Name = "labs-decode",
            };
            _decodeThread.SetApartmentState(System.Threading.ApartmentState.MTA);
            _decodeThread.Start();
        }

        L("calling labs_session_start");
        err = LabsNative.labs_session_start(_session);
        L($"labs_session_start -> {err}");
        if (err != LabsNative.LabsErrorCode.LABS_ERR_SUCCESS)
            throw new LabsException("labs_session_start failed", err);

        _started = true;
        StatusChanged?.Invoke("Session started — waiting for video…");
    }

    private int _sampleCount;

    private void DecodeLoop()
    {
        try
        {
            // Create decoder on THIS thread so all COM calls share the apartment.
            _decoder = new VideoDecoder(Hevc);
            if (!_decoder.Ready)
            {
                StatusChanged?.Invoke($"Video decoder unavailable: {_decoder.InitError}");
                return;
            }
            _decoder.FrameReady += (w, h, bytes, stride) => FrameReady?.Invoke(w, h, bytes, stride);
        }
        catch (Exception ex) { L("decoder init err: " + ex); return; }

        var reader = _decodeQueue.Reader;
        try
        {
            while (!_decoderDisposed && reader.WaitToReadAsync().AsTask().GetAwaiter().GetResult())
            {
                while (reader.TryRead(out var bytes))
                {
                    if (_decoderDisposed) break;
                    try { _decoder?.Decode(bytes); }
                    catch (Exception ex) { L("decode err: " + ex); }
                }
                if (_decoderDisposed) break;
            }
        }
        catch (Exception ex) { L("decode loop err: " + ex); }
        finally
        {
            // Dispose the decoder on the SAME thread that created it.
            try { _decoder?.Dispose(); _decoder = null; }
            catch (Exception ex) { L("decoder dispose err: " + ex); }
        }
    }
    private void OnEvent(IntPtr evtPtr, IntPtr user)
    {
        try
        {
            var type = (LabsNative.LabsEventType)Marshal.ReadInt32(evtPtr);
            L($"event: {type}");
            NativeEvent?.Invoke(type, evtPtr);
            if (type == LabsNative.LabsEventType.LABS_EVENT_QUIT)
            {
                // LabsQuitEvent is the payload immediately after the int32 type; read its reason enum.
                try
                {
                    var reasonPtr = IntPtr.Add(evtPtr, 4);
                    var reason = Marshal.ReadInt32(reasonPtr);
                    StatusChanged?.Invoke($"Disconnected — reason code {reason}. Check MTU / Wi-Fi; try wired connection.");
                }
                catch { StatusChanged?.Invoke("Disconnected."); }
            }
            else
            {
                StatusChanged?.Invoke($"Event: {type}");
            }
        }
        catch (Exception ex) { L("event err: " + ex); StatusChanged?.Invoke("Event error: " + ex.Message); }
    }

    private bool OnVideoSample(IntPtr buf, UIntPtr size, int framesLost, bool recovered, IntPtr user)
    {
        try
        {
            var len = (int)size.ToUInt32();
            var bytes = new byte[len];
            Marshal.Copy(buf, bytes, 0, len);
            if (_sampleCount < 5 || _sampleCount % 500 == 0) L($"sample #{_sampleCount} len={len} framesLost={framesLost} recovered={recovered}");
            _sampleCount++;
            SampleReady?.Invoke(bytes, recovered);
            if (!_decoderDisposed)
            {
                // Push onto the decode queue; decoder thread consumes. Never
                // call FFmpeg from the native sample thread directly.
                if (!_decodeQueue.Writer.TryWrite(bytes))
                {
                    // queue full — drop rather than back up the RP network stack
                }
            }
            return true;
        }
        catch { return false; }
    }

    public void Dispose()
    {
        L("Dispose() enter");
        _decoderDisposed = true;
        _decodeQueue.Writer.TryComplete();
        if (_started)
        {
            L("labs_session_stop"); LabsNative.labs_session_stop(_session);
            L("labs_session_join"); LabsNative.labs_session_join(_session);
            L("joined");
            _started = false;
        }
        _decodeThread?.Join(2000); // decoder disposes itself inside DecodeLoop's finally
        if (_session != IntPtr.Zero)
        {
            LabsNative.labs_session_fini(_session);
            Marshal.FreeHGlobal(_session);
            _session = IntPtr.Zero;
        }
        if (_hostStrPtr != IntPtr.Zero) { Marshal.FreeHGlobal(_hostStrPtr); _hostStrPtr = IntPtr.Zero; }
        if (_eventCbHandle.IsAllocated) _eventCbHandle.Free();
        if (_videoCbHandle.IsAllocated) _videoCbHandle.Free();
        _audio?.Dispose(); _audio = null;
        // _decoder is now disposed on its own thread in DecodeLoop.
        _logger.Dispose();
    }
}
