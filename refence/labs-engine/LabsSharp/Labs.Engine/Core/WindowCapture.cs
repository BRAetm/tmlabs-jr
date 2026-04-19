using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Windows.Graphics;
using Windows.Graphics.Capture;
using Windows.Graphics.DirectX;
using Windows.Graphics.DirectX.Direct3D11;
using WinRT;

namespace Labs.Engine.Core;

/// <summary>
/// Captures a window using the Windows Graphics Capture API (WGC).
/// Fires FrameReady on the capture thread with a frozen BitmapSource.
/// One instance per session.
/// </summary>
public sealed class WindowCapture : IDisposable
{
    /// <summary>Fires on a background thread with a frozen BitmapSource of the captured frame.</summary>
    public event Action<BitmapSource>? FrameReady;

    /// <summary>Most recent frozen BitmapSource — safe to read from any thread. Sampled by the 30fps UI timer.</summary>
    public BitmapSource? LatestFrame { get; private set; }

    private IDirect3DDevice?             _winrtDevice;
    private IntPtr                       _d3d11Device;    // ID3D11Device*
    private IntPtr                       _d3d11Context;   // ID3D11DeviceContext*
    private GraphicsCaptureItem?         _captureItem;
    private Direct3D11CaptureFramePool?  _framePool;
    private GraphicsCaptureSession?      _session;
    private SizeInt32                    _lastSize;
    private IntPtr                       _stagingTexture; // ID3D11Texture2D* reused per frame
    private int                          _stagingW, _stagingH;
    private bool                         _disposed;
    private static readonly string       _logFile = Path.Combine(AppContext.BaseDirectory, "wgc_debug.log");

    private static void Log(string msg)
    {
        var line = $"[{DateTime.Now:HH:mm:ss.fff}] {msg}";
        Console.WriteLine(line);
        try { File.AppendAllText(_logFile, line + Environment.NewLine); } catch { }
    }

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------

    /// <summary>Starts capturing the window identified by hwnd.</summary>
    public void Start(nint hwnd)
    {
        Log($"[WindowCapture] Start() called for HWND 0x{hwnd:X}");

        // 1. Create D3D11 device + wrap as WinRT IDirect3DDevice
        CreateD3D11Device(out _d3d11Device, out _d3d11Context);
        Log("[WindowCapture] D3D11 device created.");
        _winrtDevice = WrapAsWinRTDevice(_d3d11Device);
        Log("[WindowCapture] WinRT device wrapped.");

        // 2. Create GraphicsCaptureItem from window handle
        _captureItem = CreateItemForWindow(hwnd);
        _lastSize    = _captureItem.Size;
        Log($"[WindowCapture] CaptureItem created: {_lastSize.Width}x{_lastSize.Height}");

        // 3. Frame pool with 2 buffers (FreeThreaded — no DispatcherQueue needed for WPF)
        _framePool = Direct3D11CaptureFramePool.CreateFreeThreaded(
            _winrtDevice,
            DirectXPixelFormat.B8G8R8A8UIntNormalized,
            2,
            _lastSize);

        _framePool.FrameArrived += OnFrameArrived;
        Log("[WindowCapture] FramePool created, FrameArrived subscribed.");

        // 4. Start capture session
        _session = _framePool.CreateCaptureSession(_captureItem);
        _session.IsBorderRequired = false;       // no yellow border (Win11+)
        _session.IsCursorCaptureEnabled = false;  // skip cursor
        _session.StartCapture();

        Log($"[WindowCapture] Capture session started for HWND 0x{hwnd:X} ({_lastSize.Width}x{_lastSize.Height}).");
    }

    /// <summary>Stops the capture session and releases frame pool resources.</summary>
    public void Stop()
    {
        if (_session is not null)   { _session.Dispose();   _session = null; }
        if (_framePool is not null) { _framePool.Dispose(); _framePool = null; }
        _captureItem = null;
        FreeStagingTexture();
        Log("[WindowCapture] Stopped.");
    }

    /// <summary>Releases all native and managed resources.</summary>
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Stop();
        _winrtDevice?.Dispose();
        _winrtDevice = null;
        if (_d3d11Context != IntPtr.Zero) { Marshal.Release(_d3d11Context); _d3d11Context = IntPtr.Zero; }
        if (_d3d11Device  != IntPtr.Zero) { Marshal.Release(_d3d11Device);  _d3d11Device  = IntPtr.Zero; }
    }

    // ---------------------------------------------------------------------------
    // Frame processing (called on capture thread)
    // ---------------------------------------------------------------------------

    private int _frameCount;

    private void OnFrameArrived(Direct3D11CaptureFramePool sender, object args)
    {
        if (_disposed) return;

        using var frame = sender.TryGetNextFrame();
        if (frame is null) return;

        _frameCount++;
        var size = frame.ContentSize;

        if (_frameCount <= 3)
            Log($"[WindowCapture] Frame {_frameCount}: ContentSize={size.Width}x{size.Height}, LastSize={_lastSize.Width}x{_lastSize.Height}");

        // Handle window resize — recreate pool and skip this frame
        if (size.Width != _lastSize.Width || size.Height != _lastSize.Height)
        {
            Log($"[WindowCapture] Resize detected: {_lastSize.Width}x{_lastSize.Height} -> {size.Width}x{size.Height}");
            _lastSize = size;
            FreeStagingTexture();
            if (_winrtDevice is not null)
                sender.Recreate(_winrtDevice, DirectXPixelFormat.B8G8R8A8UIntNormalized, 2, size);
            return;
        }

        if (size.Width <= 0 || size.Height <= 0) return;

        try
        {
            var bitmapSource = SurfaceToBitmapSource(frame.Surface, size);
            if (bitmapSource is not null)
            {
                LatestFrame = bitmapSource;
                FrameReady?.Invoke(bitmapSource);
                if (_frameCount <= 3)
                    Log($"[WindowCapture] Frame {_frameCount}: LatestFrame set ({bitmapSource.PixelWidth}x{bitmapSource.PixelHeight})");
            }
            else if (_frameCount <= 3)
            {
                Log($"[WindowCapture] Frame {_frameCount}: SurfaceToBitmapSource returned null");
            }
        }
        catch (Exception ex)
        {
            if (_frameCount <= 5)
                Log($"[WindowCapture] Frame {_frameCount} error: {ex.Message}");
        }
    }

    /// <summary>Copies the GPU texture to a staging texture, maps it, and builds a frozen WriteableBitmap.</summary>
    private BitmapSource? SurfaceToBitmapSource(IDirect3DSurface surface, SizeInt32 size)
    {
        // Get native COM pointer from the CsWinRT projected surface
        IntPtr surfacePtr;
        if (surface is IWinRTObject winrtObj)
        {
            surfacePtr = winrtObj.NativeObject.ThisPtr;
            Marshal.AddRef(surfacePtr);
        }
        else
        {
            surfacePtr = Marshal.GetIUnknownForObject(surface);
        }

        try
        {
            // QI for IDirect3DDxgiInterfaceAccess
            var iidAccess = IID_IDirect3DDxgiInterfaceAccess;
            int qiHr = Marshal.QueryInterface(surfacePtr, ref iidAccess, out IntPtr accessPtr);
            if (qiHr < 0)
            {
                if (_frameCount <= 3) Log($"[WindowCapture] QI failed: 0x{qiHr:X8}, surfacePtr=0x{surfacePtr:X}");
                return null;
            }

            try
            {
                // GetInterface(IID_ID3D11Texture2D) via vtable slot 3
                var iidTex = IID_ID3D11Texture2D;
                IntPtr srcTexPtr = DxgiAccessVTable.GetInterface(accessPtr, ref iidTex);
                if (srcTexPtr == IntPtr.Zero)
                {
                    if (_frameCount <= 3) Log("[WindowCapture] GetInterface returned null");
                    return null;
                }

                try
                {
                    EnsureStagingTexture(size.Width, size.Height);

                    // GPU copy: source → staging
                    D3D11VTable.CopyResource(_d3d11Context, _stagingTexture, srcTexPtr);

                    // Map staging texture for CPU read
                    int hr = D3D11VTable.Map(_d3d11Context, _stagingTexture, 0, D3D11_MAP_READ, 0, out var mapped);
                    if (hr < 0) return null;

                    try
                    {
                        int w = size.Width, h = size.Height;
                        var wb = new WriteableBitmap(w, h, 96, 96, PixelFormats.Bgra32, null);
                        wb.Lock();
                        unsafe
                        {
                            byte* dst = (byte*)wb.BackBuffer;
                            byte* src = (byte*)mapped.pData;
                            int dstStride = wb.BackBufferStride;
                            int rowBytes  = w * 4;

                            for (int row = 0; row < h; row++)
                                Buffer.MemoryCopy(
                                    src + (long)row * mapped.RowPitch,
                                    dst + (long)row * dstStride,
                                    dstStride, rowBytes);
                        }
                        wb.AddDirtyRect(new System.Windows.Int32Rect(0, 0, w, h));
                        wb.Unlock();
                        wb.Freeze();
                        return wb;
                    }
                    finally
                    {
                        D3D11VTable.Unmap(_d3d11Context, _stagingTexture, 0);
                    }
                }
                finally
                {
                    Marshal.Release(srcTexPtr);
                }
            }
            finally
            {
                Marshal.Release(accessPtr);
            }
        }
        finally
        {
            Marshal.Release(surfacePtr);
        }
    }

    // ---------------------------------------------------------------------------
    // Staging texture management
    // ---------------------------------------------------------------------------

    private void EnsureStagingTexture(int w, int h)
    {
        if (_stagingTexture != IntPtr.Zero && _stagingW == w && _stagingH == h)
            return;

        FreeStagingTexture();

        var desc = new D3D11_TEXTURE2D_DESC
        {
            Width          = (uint)w,
            Height         = (uint)h,
            MipLevels      = 1,
            ArraySize      = 1,
            Format         = DXGI_FORMAT_B8G8R8A8_UNORM,
            SampleCount    = 1,
            SampleQuality  = 0,
            Usage          = D3D11_USAGE_STAGING,
            BindFlags      = 0,
            CPUAccessFlags = D3D11_CPU_ACCESS_READ,
            MiscFlags      = 0,
        };

        int hr = D3D11VTable.CreateTexture2D(_d3d11Device, ref desc, IntPtr.Zero, out _stagingTexture);
        Marshal.ThrowExceptionForHR(hr);
        _stagingW = w;
        _stagingH = h;
    }

    private void FreeStagingTexture()
    {
        if (_stagingTexture != IntPtr.Zero)
        {
            Marshal.Release(_stagingTexture);
            _stagingTexture = IntPtr.Zero;
        }
    }

    // ---------------------------------------------------------------------------
    // D3D11 device creation + WinRT wrapping
    // ---------------------------------------------------------------------------

    private static void CreateD3D11Device(out IntPtr device, out IntPtr context)
    {
        int hr = D3D11CreateDevice(
            IntPtr.Zero,
            D3D_DRIVER_TYPE_HARDWARE,
            IntPtr.Zero,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            null, 0,
            D3D11_SDK_VERSION,
            out device, out _, out context);
        Marshal.ThrowExceptionForHR(hr);
    }

    private static IDirect3DDevice WrapAsWinRTDevice(IntPtr d3d11Device)
    {
        var iid = IID_IDXGIDevice;
        Marshal.ThrowExceptionForHR(Marshal.QueryInterface(d3d11Device, ref iid, out IntPtr dxgiDevice));
        try
        {
            CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, out IntPtr inspectable);
            try
            {
                return MarshalInterface<IDirect3DDevice>.FromAbi(inspectable);
            }
            finally { Marshal.Release(inspectable); }
        }
        finally { Marshal.Release(dxgiDevice); }
    }

    // ---------------------------------------------------------------------------
    // GraphicsCaptureItem from HWND (via IGraphicsCaptureItemInterop)
    // ---------------------------------------------------------------------------

    private static GraphicsCaptureItem CreateItemForWindow(nint hwnd)
    {
        const string className = "Windows.Graphics.Capture.GraphicsCaptureItem";

        // Create HSTRING for the WinRT class name
        WindowsCreateString(className, className.Length, out IntPtr hstr);
        try
        {
            // Get the activation factory, QI'd for IGraphicsCaptureItemInterop
            var interopIid = typeof(IGraphicsCaptureItemInterop).GUID;
            RoGetActivationFactory(hstr, ref interopIid, out IntPtr factoryPtr);
            var interop = (IGraphicsCaptureItemInterop)Marshal.GetObjectForIUnknown(factoryPtr);
            Marshal.Release(factoryPtr);

            // Create the capture item from the window handle
            var itemIid = IID_IGraphicsCaptureItem;
            int hr = interop.CreateForWindow(hwnd, ref itemIid, out IntPtr itemPtr);
            Marshal.ThrowExceptionForHR(hr);

            var item = MarshalInspectable<GraphicsCaptureItem>.FromAbi(itemPtr);
            Marshal.Release(itemPtr);
            return item;
        }
        finally { WindowsDeleteString(hstr); }
    }

    // ---------------------------------------------------------------------------
    // COM interop interfaces
    // ---------------------------------------------------------------------------

    [ComImport, Guid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IDirect3DDxgiInterfaceAccess
    {
        void GetInterface([In] ref Guid iid, out IntPtr p);
    }

    [ComImport, Guid("3628E81B-3CAC-4C60-B7F4-23CE0E0C3356"),
     InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IGraphicsCaptureItemInterop
    {
        [PreserveSig]
        int CreateForWindow(IntPtr window, ref Guid iid, out IntPtr result);

        [PreserveSig]
        int CreateForMonitor(IntPtr monitor, ref Guid iid, out IntPtr result);
    }

    // ---------------------------------------------------------------------------
    // IDirect3DDxgiInterfaceAccess vtable helper (slot 3 = GetInterface)
    // ---------------------------------------------------------------------------

    private static class DxgiAccessVTable
    {
        /// <summary>IDirect3DDxgiInterfaceAccess::GetInterface — vtable slot 3.</summary>
        public static IntPtr GetInterface(IntPtr accessPtr, ref Guid iid)
        {
            IntPtr vtbl  = Marshal.ReadIntPtr(accessPtr);
            IntPtr fnPtr = Marshal.ReadIntPtr(vtbl, 3 * IntPtr.Size); // slot 3 after QI/AddRef/Release
            var fn = Marshal.GetDelegateForFunctionPointer<FnGetInterface>(fnPtr);
            int hr = fn(accessPtr, ref iid, out IntPtr result);
            Marshal.ThrowExceptionForHR(hr);
            return result;
        }

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int FnGetInterface(IntPtr self, ref Guid iid, out IntPtr p);
    }

    // ---------------------------------------------------------------------------
    // D3D11 vtable helpers (call native COM methods by slot index)
    // ---------------------------------------------------------------------------

    private static class D3D11VTable
    {
        /// <summary>ID3D11Device::CreateTexture2D — vtable slot 5.</summary>
        public static int CreateTexture2D(IntPtr device, ref D3D11_TEXTURE2D_DESC desc, IntPtr init, out IntPtr tex)
            => GetMethod<FnCreateTexture2D>(device, 5)(device, ref desc, init, out tex);

        /// <summary>ID3D11DeviceContext::Map — vtable slot 14.</summary>
        public static int Map(IntPtr ctx, IntPtr res, uint sub, int mapType, uint flags, out D3D11_MAPPED_SUBRESOURCE mapped)
            => GetMethod<FnMap>(ctx, 14)(ctx, res, sub, mapType, flags, out mapped);

        /// <summary>ID3D11DeviceContext::Unmap — vtable slot 15.</summary>
        public static void Unmap(IntPtr ctx, IntPtr res, uint sub)
            => GetMethod<FnUnmap>(ctx, 15)(ctx, res, sub);

        /// <summary>ID3D11DeviceContext::CopyResource — vtable slot 47.</summary>
        public static void CopyResource(IntPtr ctx, IntPtr dst, IntPtr src)
            => GetMethod<FnCopyResource>(ctx, 47)(ctx, dst, src);

        private static T GetMethod<T>(IntPtr comObj, int slot) where T : Delegate
        {
            IntPtr vtbl  = Marshal.ReadIntPtr(comObj);
            IntPtr fnPtr = Marshal.ReadIntPtr(vtbl, slot * IntPtr.Size);
            return Marshal.GetDelegateForFunctionPointer<T>(fnPtr);
        }

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int FnCreateTexture2D(IntPtr self, ref D3D11_TEXTURE2D_DESC desc, IntPtr init, out IntPtr ppTex);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int FnMap(IntPtr self, IntPtr res, uint sub, int mapType, uint flags, out D3D11_MAPPED_SUBRESOURCE mapped);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void FnUnmap(IntPtr self, IntPtr res, uint sub);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void FnCopyResource(IntPtr self, IntPtr dst, IntPtr src);
    }

    // ---------------------------------------------------------------------------
    // Native structs
    // ---------------------------------------------------------------------------

    [StructLayout(LayoutKind.Sequential)]
    private struct D3D11_TEXTURE2D_DESC
    {
        public uint Width, Height, MipLevels, ArraySize;
        public uint Format;
        public uint SampleCount, SampleQuality; // DXGI_SAMPLE_DESC inlined
        public uint Usage;
        public uint BindFlags;
        public uint CPUAccessFlags;
        public uint MiscFlags;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct D3D11_MAPPED_SUBRESOURCE
    {
        public IntPtr pData;
        public uint   RowPitch;
        public uint   DepthPitch;
    }

    // ---------------------------------------------------------------------------
    // Constants + GUIDs
    // ---------------------------------------------------------------------------

    private const int  D3D_DRIVER_TYPE_HARDWARE          = 1;
    private const uint D3D11_CREATE_DEVICE_BGRA_SUPPORT  = 0x20;
    private const uint D3D11_SDK_VERSION                 = 7;
    private const uint DXGI_FORMAT_B8G8R8A8_UNORM        = 87;
    private const uint D3D11_USAGE_STAGING               = 3;
    private const uint D3D11_CPU_ACCESS_READ             = 0x20000;
    private const int  D3D11_MAP_READ                    = 1;

    private static readonly Guid IID_IDXGIDevice                    = new("54EC77FA-1377-44E6-8C32-88FD5F44C84C");
    private static readonly Guid IID_ID3D11Texture2D                = new("6F15AAF2-D208-4E89-9AB4-489535D34F9C");
    private static readonly Guid IID_IGraphicsCaptureItem           = new("79C3F95B-31F7-4EC2-A464-632EF5D30760");
    private static readonly Guid IID_IDirect3DDxgiInterfaceAccess   = new("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1");

    // ---------------------------------------------------------------------------
    // P/Invoke
    // ---------------------------------------------------------------------------

    [DllImport("d3d11.dll", ExactSpelling = true, PreserveSig = true)]
    private static extern int D3D11CreateDevice(
        IntPtr pAdapter, int driverType, IntPtr software, uint flags,
        int[]? featureLevels, uint featureLevelCount, uint sdkVersion,
        out IntPtr ppDevice, out int pFeatureLevel, out IntPtr ppContext);

    [DllImport("d3d11.dll", EntryPoint = "CreateDirect3D11DeviceFromDXGIDevice",
               ExactSpelling = true, PreserveSig = false)]
    private static extern void CreateDirect3D11DeviceFromDXGIDevice(
        IntPtr dxgiDevice, out IntPtr graphicsDevice);

    [DllImport("combase.dll", ExactSpelling = true, PreserveSig = false)]
    private static extern void RoGetActivationFactory(
        IntPtr activatableClassId, ref Guid iid, out IntPtr factory);

    [DllImport("combase.dll", CharSet = CharSet.Unicode, ExactSpelling = true, PreserveSig = false)]
    private static extern void WindowsCreateString(
        string sourceString, int length, out IntPtr hstring);

    [DllImport("combase.dll", ExactSpelling = true)]
    private static extern int WindowsDeleteString(IntPtr hstring);
}
