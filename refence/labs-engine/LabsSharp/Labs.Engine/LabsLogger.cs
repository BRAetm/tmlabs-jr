using System;
using System.Runtime.InteropServices;
using Labs.Native;

namespace Labs.Engine;

/// <summary>
/// Creates and owns a <see cref="LabsNative.LabsLog"/> struct that routes to a managed
/// delegate. The struct must outlive any native call that uses it — hold a reference.
/// </summary>
public sealed class LabsLogger : IDisposable
{
    public LabsNative.LabsLog Native;
    private readonly LabsNative.LabsLogCb? _cb;
    private readonly GCHandle _cbHandle;

    public event Action<LabsNative.LabsLogLevel, string>? Message;

    public LabsLogger(LabsNative.LabsLogLevel levelMask =
        LabsNative.LabsLogLevel.LABS_LOG_INFO |
        LabsNative.LabsLogLevel.LABS_LOG_WARNING |
        LabsNative.LabsLogLevel.LABS_LOG_ERROR)
    {
        Native = default;
        _cb = OnLog;
        _cbHandle = GCHandle.Alloc(_cb);
        var cbPtr = Marshal.GetFunctionPointerForDelegate(_cb);
        LabsNative.labs_log_init(ref Native, (uint)levelMask, cbPtr, IntPtr.Zero);
    }

    private void OnLog(LabsNative.LabsLogLevel level, IntPtr msg, IntPtr user)
    {
        var text = Marshal.PtrToStringUTF8(msg) ?? "";
        Message?.Invoke(level, text);
    }

    public void Dispose()
    {
        if (_cbHandle.IsAllocated) _cbHandle.Free();
    }
}
