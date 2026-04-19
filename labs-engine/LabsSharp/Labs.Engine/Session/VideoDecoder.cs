using System;
using System.Runtime.InteropServices;
using SharpGen.Runtime;
using Vortice.MediaFoundation;
using static Vortice.MediaFoundation.MediaFactory;

namespace Labs.Engine.Session;

/// <summary>
/// H.264 / H.265 → BGRA decoder backed by Windows Media Foundation. Uses the
/// Microsoft built-in H.264 / HEVC decoder MFT (hardware-accelerated when
/// available). No external runtime DLLs required — Media Foundation ships
/// with Windows.
///
/// Flow:  raw Annex-B sample → IMFSample (input) → ProcessInput → ProcessOutput
///        → IMFMediaBuffer (NV12) → C# NV12→BGRA → <see cref="FrameReady"/>.
/// </summary>
public sealed class VideoDecoder : IDisposable
{
    // CLSIDs for the built-in Microsoft decoder MFTs.
    private static readonly Guid CLSID_MSH264DecoderMFT = new("62CE7E72-4C71-4D20-B15D-452831A87D9D");
    private static readonly Guid CLSID_MSH265DecoderMFT = new("420A51A3-D605-430C-B4FC-45274FA6C562");

    // MFVideoFormat_* GUIDs (not exposed by Vortice 3.6.2).
    private static readonly Guid MFVideoFormat_H264 = new("34363248-0000-0010-8000-00AA00389B71");
    private static readonly Guid MFVideoFormat_HEVC = new("43564548-0000-0010-8000-00AA00389B71");
    private static readonly Guid MFVideoFormat_NV12 = new("3231564E-0000-0010-8000-00AA00389B71");

    private static readonly Guid IID_IMFTransform = new("BF94C121-5B05-4E6F-8000-BA598961414D");

    // HRESULTs.
    private const int MF_E_TRANSFORM_NEED_MORE_INPUT = unchecked((int)0xC00D6D72);
    private const int MF_E_TRANSFORM_STREAM_CHANGE   = unchecked((int)0xC00D6D61);

    private readonly bool _ready;
    private readonly IMFTransform _decoder = null!;
    private readonly object _gate = new();

    private int _width, _height;
    private int _srcStride;   // NV12 row pitch as reported by the MFT (often padded)
    private byte[]? _bgraBuf;
    private int _bgraStride;

    public delegate void FrameCallback(int width, int height, byte[] bgra, int stride);
    public event FrameCallback? FrameReady;

    public bool Ready => _ready;
    public string? InitError { get; }

    public VideoDecoder(bool hevc)
    {
        try
        {
            // Vortice hides the version arg. Parameter selects Lite (true) vs Full (false).
            MFStartup(true).CheckError();

            var clsid = hevc ? CLSID_MSH265DecoderMFT : CLSID_MSH264DecoderMFT;
            var inSubtype = hevc ? MFVideoFormat_HEVC : MFVideoFormat_H264;

            _decoder = CreateMft(clsid);

            // Force software decoding. DXVA hands us D3D surfaces in NV12 that
            // aren't safe to read from CPU/managed code — the Microsoft H.264 MFT
            // will fall back to a pure-software path when DXVA is disabled.
            TryDisableDxva(_decoder);

            using (var inType = MFCreateMediaType())
            {
                inType.Set(MediaTypeAttributeKeys.MajorType, MediaTypeGuids.Video);
                inType.Set(MediaTypeAttributeKeys.Subtype, inSubtype);
                inType.Set(MediaTypeAttributeKeys.InterlaceMode, (uint)VideoInterlaceMode.Progressive);
                _decoder.SetInputType(0, inType, 0);
            }

            SelectNv12OutputType();

            _decoder.ProcessMessage(TMessageType.MessageCommandFlush, UIntPtr.Zero);
            _decoder.ProcessMessage(TMessageType.MessageNotifyBeginStreaming, UIntPtr.Zero);
            _decoder.ProcessMessage(TMessageType.MessageNotifyStartOfStream, UIntPtr.Zero);
            _ready = true;
        }
        catch (Exception ex)
        {
            InitError = ex.Message;
            _ready = false;
        }
    }

    private static IMFTransform CreateMft(Guid clsid)
    {
        int hr = CoCreateInstance(clsid, IntPtr.Zero, 0x1 /* CLSCTX_INPROC_SERVER */,
                                  IID_IMFTransform, out IntPtr unk);
        if (hr < 0 || unk == IntPtr.Zero) Marshal.ThrowExceptionForHR(hr);
        return new IMFTransform(unk);
    }

    [DllImport("ole32.dll", ExactSpelling = true)]
    private static extern int CoCreateInstance(
        in Guid rclsid, IntPtr pUnkOuter, uint dwClsContext,
        in Guid riid, out IntPtr ppv);

    // ICodecAPI interface + CODECAPI property for disabling H.264 HW acceleration.
    private static readonly Guid IID_ICodecAPI = new("901DB4C7-31CE-41A2-85DC-8FA0BF41B8DA");
    private static readonly Guid CODECAPI_AVDecVideoAcceleration_H264 =
        new("F7DB8A2F-4F48-4EE8-AE31-8B6EBE558AE2");

    private static void TryDisableDxva(IMFTransform mft)
    {
        try
        {
            var unk = Marshal.GetIUnknownForObject(mft);
            try
            {
                var hr = Marshal.QueryInterface(unk, IID_ICodecAPI, out var codecApi);
                if (hr < 0 || codecApi == IntPtr.Zero) return;
                try { SetCodecBoolFalse(codecApi, CODECAPI_AVDecVideoAcceleration_H264); }
                finally { Marshal.Release(codecApi); }
            }
            finally { Marshal.Release(unk); }
        }
        catch { /* best-effort; if this fails, hardware accel may still be on */ }
    }

    // VARIANT_BOOL/BOOL-style VARIANT containing a VT_UI4 = 0 for "off".
    private static void SetCodecBoolFalse(IntPtr codecApi, Guid propGuid)
    {
        // vtable slot 8 for ICodecAPI::SetValue(REFGUID, const VARIANT*)
        // IUnknown: 0..2, ICodecAPI: IsSupported, IsModifiable, GetParameterRange, GetParameterValues,
        //   GetDefaultValue, GetValue, SetValue ...
        var vtbl = Marshal.ReadIntPtr(codecApi);
        var setValueFn = Marshal.ReadIntPtr(vtbl, IntPtr.Size * 8);

        // VARIANT with VT_UI4 = 0
        Span<byte> variant = stackalloc byte[24];
        variant.Clear();
        variant[0] = 0x13; // VT_UI4
        // lVal/uintVal at offset 8 stays 0.

        unsafe
        {
            fixed (byte* vPtr = variant)
            {
                var setValue = (delegate* unmanaged[Stdcall]<IntPtr, Guid*, byte*, int>)setValueFn;
                var g = propGuid;
                _ = setValue(codecApi, &g, vPtr);
            }
        }
    }

    private void SelectNv12OutputType()
    {
        for (int i = 0; ; i++)
        {
            IMFMediaType? outType = null;
            try { outType = _decoder.GetOutputAvailableType(0, i); }
            catch { throw new InvalidOperationException("No NV12 output type available."); }

            try
            {
                var subtype = outType.GetGUID(MediaTypeAttributeKeys.Subtype);
                if (subtype == MFVideoFormat_NV12)
                {
                    _decoder.SetOutputType(0, outType, 0);
                    TryReadFrameSize(outType);
                    return;
                }
            }
            finally { outType.Dispose(); }
        }
    }

    // MF_MT_DEFAULT_STRIDE attribute key ({644B4E48-2E02-4C4E-97DE-BC4B0B0BE63E})
    private static readonly Guid MF_MT_DEFAULT_STRIDE = new("644B4E48-2E02-4C4E-97DE-BC4B0B0BE63E");

    private void TryReadFrameSize(IMFMediaType type)
    {
        try
        {
            ulong size = type.GetUInt64(MediaTypeAttributeKeys.FrameSize);
            _width = (int)(size >> 32);
            _height = (int)(size & 0xFFFFFFFFu);
        }
        catch { /* size not known yet — will come on stream-change */ }
        try
        {
            // stride is SIGNED in MF — negative means bottom-up. Get raw uint then reinterpret.
            _srcStride = unchecked((int)type.GetUInt32(MF_MT_DEFAULT_STRIDE));
            if (_srcStride < 0) _srcStride = -_srcStride; // treat bottom-up as positive; row order handled per-buffer
            if (_srcStride < _width) _srcStride = _width;
        }
        catch { _srcStride = _width; }
    }

    private static readonly Guid IID_IMF2DBuffer = new("7DC9D5F9-9ED9-44EC-9BBF-0600BB589FBB");

    // Returns true + stride if the buffer exposes IMF2DBuffer (authoritative pitch).
    private static bool TryGet2DStride(IMFMediaBuffer buf, out int stride)
    {
        stride = 0;
        var unk = Marshal.GetIUnknownForObject(buf);
        try
        {
            var hr = Marshal.QueryInterface(unk, IID_IMF2DBuffer, out var p2d);
            if (hr < 0 || p2d == IntPtr.Zero) return false;
            try
            {
                // IMF2DBuffer vtable: IUnknown(3) + Lock2D, Unlock2D, GetScanline0AndPitch, IsContiguousFormat, ...
                // We just want pitch without locking: call GetScanline0AndPitch but it needs lock first.
                // Easier: Lock2D then immediately Unlock2D.
                var vtbl = Marshal.ReadIntPtr(p2d);
                var lock2DFn = Marshal.ReadIntPtr(vtbl, IntPtr.Size * 3);
                var unlock2DFn = Marshal.ReadIntPtr(vtbl, IntPtr.Size * 4);
                unsafe
                {
                    IntPtr scan0 = IntPtr.Zero; int pitch = 0;
                    var lock2D = (delegate* unmanaged[Stdcall]<IntPtr, out IntPtr, out int, int>)lock2DFn;
                    if (lock2D(p2d, out scan0, out pitch) < 0) return false;
                    var unlock2D = (delegate* unmanaged[Stdcall]<IntPtr, int>)unlock2DFn;
                    _ = unlock2D(p2d);
                    stride = Math.Abs(pitch);
                    return stride > 0;
                }
            }
            finally { Marshal.Release(p2d); }
        }
        finally { Marshal.Release(unk); }
    }

    public void Decode(byte[] sample)
    {
        if (!_ready || sample.Length == 0) return;
        lock (_gate)
        {
            try
            {
                var mediaBuf = MFCreateMemoryBuffer(sample.Length);
                mediaBuf.Lock(out IntPtr pin, out _, out _);
                Marshal.Copy(sample, 0, pin, sample.Length);
                mediaBuf.Unlock();
                mediaBuf.CurrentLength = sample.Length;

                var input = MFCreateSample();
                input.AddBuffer(mediaBuf);

                _decoder.ProcessInput(0, input, 0);

                input.Dispose();
                mediaBuf.Dispose();

                DrainOutput();
            }
            catch { /* swallow — a bad packet should never kill the app */ }
        }
    }

    private void DrainOutput()
    {
        while (true)
        {
            var info = _decoder.GetOutputStreamInfo(0);
            int bufLen = Math.Max(info.Size, 1920 * 1080 * 3 / 2);
            var outBuf = MFCreateMemoryBuffer(bufLen);
            var outSample = MFCreateSample();
            outSample.AddBuffer(outBuf);

            OutputDataBuffer odb = default;
            odb.StreamID = 0;
            odb.Sample = outSample;

            var result = _decoder.ProcessOutput(ProcessOutputFlags.None, 1, ref odb, out _);

            try
            {
                if (result.Code == MF_E_TRANSFORM_NEED_MORE_INPUT) return;
                if (result.Code == MF_E_TRANSFORM_STREAM_CHANGE)
                {
                    SelectNv12OutputType();
                    continue;
                }
                if (result.Failure) return;

                EmitFrom(outBuf);
            }
            finally
            {
                outSample.Dispose();
                outBuf.Dispose();
            }
        }
    }

    private void EmitFrom(IMFMediaBuffer buf)
    {
        if (_width <= 0 || _height <= 0)
        {
            // Try to pick up size from the current output type.
            try
            {
                using var cur = _decoder.GetOutputCurrentType(0);
                TryReadFrameSize(cur);
            }
            catch { }
            if (_width <= 0 || _height <= 0) return;
        }

        // Prefer the 2D buffer's own pitch — it's what the decoder actually wrote.
        if (TryGet2DStride(buf, out var pitch)) _srcStride = pitch;

        buf.Lock(out IntPtr dataPtr, out int maxLen, out _);
        try
        {
            // Verify the buffer is big enough for our assumed NV12 layout before we read.
            var stride = _srcStride > 0 ? _srcStride : _width;
            var expected = stride * _height * 3 / 2;
            if (maxLen < expected) return;
            EnsureBgraBuffer(_width, _height);
            unsafe
            {
                fixed (byte* dst = _bgraBuf)
                    Nv12ToBgra((byte*)dataPtr, _width, _height, stride, dst, _bgraStride);
            }
            var snapshot = (byte[])_bgraBuf!.Clone();
            FrameReady?.Invoke(_width, _height, snapshot, _bgraStride);
        }
        finally { buf.Unlock(); }
    }

    private void EnsureBgraBuffer(int w, int h)
    {
        var stride = w * 4;
        if (_bgraBuf != null && stride == _bgraStride && _bgraBuf.Length == stride * h) return;
        _bgraBuf = new byte[stride * h];
        _bgraStride = stride;
    }

    // BT.709 limited-range NV12 → BGRA. Good enough for remote play at 1080p.
    // NV12 -> BGRA. Unrolled to process two pixels per iteration, sharing the
    // UV sample (NV12 chroma is 2:1 subsampled). BT.709 limited range.
    private static unsafe void Nv12ToBgra(byte* src, int w, int h, int srcStride, byte* dst, int dstStride)
    {
        byte* ySrc = src;
        byte* uvSrc = src + srcStride * h;
        int wEven = w & ~1;
        for (int y = 0; y < h; y++)
        {
            byte* dstRow = dst + y * dstStride;
            byte* yRow = ySrc + y * srcStride;
            byte* uvRow = uvSrc + (y >> 1) * srcStride;

            int x = 0;
            for (; x < wEven; x += 2)
            {
                int U = uvRow[x    ] - 128;
                int V = uvRow[x + 1] - 128;
                int gu = 55 * U;
                int gv = 136 * V;
                int rv = 459 * V;
                int bu = 541 * U;

                int Y0 = (yRow[x]     - 16) * 298 + 128;
                int Y1 = (yRow[x + 1] - 16) * 298 + 128;

                int r0 = (Y0 + rv) >> 8, g0 = (Y0 - gu - gv) >> 8, b0 = (Y0 + bu) >> 8;
                int r1 = (Y1 + rv) >> 8, g1 = (Y1 - gu - gv) >> 8, b1 = (Y1 + bu) >> 8;

                dstRow[0] = Clamp(b0); dstRow[1] = Clamp(g0); dstRow[2] = Clamp(r0); dstRow[3] = 255;
                dstRow[4] = Clamp(b1); dstRow[5] = Clamp(g1); dstRow[6] = Clamp(r1); dstRow[7] = 255;
                dstRow += 8;
            }
            if (x < w)
            {
                int U = uvRow[x & ~1]     - 128;
                int V = uvRow[(x & ~1)+1] - 128;
                int Y = (yRow[x] - 16) * 298 + 128;
                dstRow[0] = Clamp((Y + 541 * U) >> 8);
                dstRow[1] = Clamp((Y - 55 * U - 136 * V) >> 8);
                dstRow[2] = Clamp((Y + 459 * V) >> 8);
                dstRow[3] = 255;
            }
        }
    }

    private static byte Clamp(int v) => v < 0 ? (byte)0 : v > 255 ? (byte)255 : (byte)v;

    public void Dispose()
    {
        lock (_gate)
        {
            try { _decoder?.ProcessMessage(TMessageType.MessageNotifyEndOfStream, UIntPtr.Zero); } catch { }
            try { _decoder?.ProcessMessage(TMessageType.MessageNotifyEndStreaming, UIntPtr.Zero); } catch { }
            _decoder?.Dispose();
            _bgraBuf = null;
            try { MFShutdown(); } catch { }
        }
    }
}
