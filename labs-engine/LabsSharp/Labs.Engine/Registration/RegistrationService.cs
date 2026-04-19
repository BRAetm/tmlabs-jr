using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Labs.Engine.Discovery;
using Labs.Native;

namespace Labs.Engine.Registration;

/// <summary>
/// PS5 LAN pairing via labs_regist_start. The native side runs registration on
/// a worker thread and fires a callback with the resulting credentials.
/// </summary>
public static class RegistrationService
{
    // Upper bound for sizeof(LabsRegist) — padded generously vs layout in regist.h
    // (LabsLog* + LabsRegistInfo + cb + user + LabsThread + LabsStopPipe).
    private const int LabsRegistSize = 2048;

    public static async Task<RegisteredCredentials> RegisterAsync(
        DiscoveredHost host, string accountIdBase64, string pin, CancellationToken ct)
    {
        LabsRuntime.Initialize();

        var accountId = Convert.FromBase64String(accountIdBase64);
        if (accountId.Length != LabsNative.LABS_PSN_ACCOUNT_ID_SIZE)
            throw new ArgumentException(
                $"PSN account ID must decode to {LabsNative.LABS_PSN_ACCOUNT_ID_SIZE} bytes " +
                $"(got {accountId.Length}).", nameof(accountIdBase64));
        if (!uint.TryParse(pin, out var pinNum) || pin.Length != 8)
            throw new ArgumentException("PIN must be 8 digits.", nameof(pin));

        using var logger = new LabsLogger();
        var tcs = new TaskCompletionSource<RegisteredCredentials>(TaskCreationOptions.RunContinuationsAsynchronously);

        // Pin the callback (native thread fires it).
        LabsNative.LabsRegistCb cb = (evtPtr, _) =>
        {
            try
            {
                var evt = Marshal.PtrToStructure<LabsNative.LabsRegistEvent>(evtPtr);
                switch (evt.type)
                {
                    case LabsNative.LabsRegistEventType.LABS_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
                        var hostStruct = Marshal.PtrToStructure<LabsNative.LabsRegisteredHost>(evt.registered_host);
                        tcs.TrySetResult(ToManaged(host.HostId, hostStruct));
                        break;
                    case LabsNative.LabsRegistEventType.LABS_REGIST_EVENT_TYPE_FINISHED_CANCELED:
                        tcs.TrySetCanceled();
                        break;
                    case LabsNative.LabsRegistEventType.LABS_REGIST_EVENT_TYPE_FINISHED_FAILED:
                    default:
                        tcs.TrySetException(new LabsException("Registration failed",
                            LabsNative.LabsErrorCode.LABS_ERR_UNKNOWN));
                        break;
                }
            }
            catch (Exception ex) { tcs.TrySetException(ex); }
        };
        var cbHandle = GCHandle.Alloc(cb);
        var cbPtr = Marshal.GetFunctionPointerForDelegate(cb);

        // Allocate native LabsRegist + host string.
        var regist = Marshal.AllocHGlobal(LabsRegistSize);
        unsafe { new Span<byte>((void*)regist, LabsRegistSize).Clear(); }
        var hostStrPtr = Marshal.StringToHGlobalAnsi(host.Address.ToString());

        try
        {
            var info = new LabsNative.LabsRegistInfo
            {
                target = LabsNative.LabsTarget.LABS_TARGET_PS5_1,
                host = hostStrPtr,
                broadcast = false,
                psn_online_id = IntPtr.Zero,
                pin = pinNum,
                console_pin = 0,
                holepunch_info = IntPtr.Zero,
            };
            unsafe
            {
                for (int i = 0; i < LabsNative.LABS_PSN_ACCOUNT_ID_SIZE; i++)
                    info.psn_account_id[i] = accountId[i];
            }

            var err = LabsNative.labs_regist_start(regist, ref logger.Native, ref info, cbPtr, IntPtr.Zero);
            if (err != LabsNative.LabsErrorCode.LABS_ERR_SUCCESS)
                throw new LabsException("labs_regist_start failed", err);

            using (ct.Register(() =>
            {
                LabsNative.labs_regist_stop(regist);
                tcs.TrySetCanceled(ct);
            }))
            {
                return await tcs.Task.ConfigureAwait(false);
            }
        }
        finally
        {
            LabsNative.labs_regist_fini(regist);
            Marshal.FreeHGlobal(regist);
            Marshal.FreeHGlobal(hostStrPtr);
            cbHandle.Free();
        }
    }

    private static unsafe RegisteredCredentials ToManaged(string hostId, LabsNative.LabsRegisteredHost h)
    {
        static string FixedAsciiString(byte* p, int max)
        {
            int len = 0;
            while (len < max && p[len] != 0) len++;
            return Encoding.ASCII.GetString(p, len);
        }
        static byte[] FixedBytes(byte* p, int len)
        {
            var arr = new byte[len];
            Marshal.Copy((IntPtr)p, arr, 0, len);
            return arr;
        }

        return new RegisteredCredentials(
            HostId:        hostId,
            ApSsid:        FixedAsciiString(h.ap_ssid, 0x30),
            ApBssid:       FixedAsciiString(h.ap_bssid, 0x20),
            ApKey:         FixedAsciiString(h.ap_key, 0x50),
            ApName:        FixedAsciiString(h.ap_name, 0x20),
            ServerNonce:   Array.Empty<byte>(),
            ClientNonce:   Array.Empty<byte>(),
            RpRegistKey:   FixedBytes(h.rp_regist_key, LabsNative.LABS_SESSION_AUTH_SIZE),
            RpKeyType:     h.rp_key_type,
            RpKey:         FixedBytes(h.rp_key, 0x10));
    }
}

public sealed record RegisteredCredentials(
    string HostId,
    string ApSsid,
    string ApBssid,
    string ApKey,
    string ApName,
    byte[] ServerNonce,
    byte[] ClientNonce,
    byte[] RpRegistKey,
    uint RpKeyType,
    byte[] RpKey);
