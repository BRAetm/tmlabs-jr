using System;
using Labs.Native;
using LabsErrorCode = Labs.Native.LabsNative.LabsErrorCode;

namespace Labs.Engine;

/// <summary>One-shot initialization of the native labs engine. Call <see cref="Initialize"/> once at app start.</summary>
public static class LabsRuntime
{
    private static bool _initialized;
    private static readonly object _gate = new();

    public static void Initialize()
    {
        lock (_gate)
        {
            if (_initialized) return;
            var err = LabsNative.labs_lib_init();
            if (err != LabsErrorCode.LABS_ERR_SUCCESS)
                throw new LabsException("labs_lib_init failed", err);
            _initialized = true;
        }
    }
}

public sealed class LabsException : Exception
{
    public LabsErrorCode Code { get; }
    public LabsException(string message, LabsErrorCode code) : base($"{message} (code={code})")
    {
        Code = code;
    }
}
