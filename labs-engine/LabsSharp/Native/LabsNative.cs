// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// P/Invoke bindings for labs.dll (PS Remote Play native library).
//
// This file is hand-rolled and only exposes the subset of the native API
// needed for the managed LabsSharp layer to:
//   - Initialize the library
//   - Discover PS5 consoles on LAN
//   - Register (pair) with a PS5
//   - Start/stop a streaming session
//   - Send controller input
//   - Receive video / audio frame callbacks
//
// The native side uses many opaque-ish structs (sessions, discovery threads,
// log sniffers, etc). Where the manage side doesn't need to introspect them,
// they are represented either as IntPtr handles or as opaque fixed-size
// blobs allocated by the caller. For the few structs that the managed side
// *does* fill out (ConnectInfo, ControllerState, DiscoveryHost, ...) fully
// sequential layouts are provided.
//
// Buffer allocation note: LabsSession is large and its layout depends on
// internal headers (thread primitives, ctrl state, stream connection, etc).
// The managed side should NOT `new LabsSession` — instead it must allocate
// a sufficient byte buffer (e.g. Marshal.AllocHGlobal(LabsNative.LabsSessionSizeMax))
// and pass that pointer. The recommended approach is to compile a tiny C
// helper that returns sizeof(LabsSession) at runtime and call that once.

using System;
using System.Runtime.InteropServices;

namespace Labs.Native
{
    public static unsafe class LabsNative
    {
        public const string Dll = "labs.dll";

        // ------------------------------------------------------------------
        // Constants (from common.h, regist.h, session.h, discovery.h, feedback.h)
        // ------------------------------------------------------------------

        public const int LABS_PSN_ACCOUNT_ID_SIZE = 8;
        public const int LABS_SESSION_AUTH_SIZE = 0x10;
        public const int LABS_RPCRYPT_KEY_SIZE = 0x10;
        public const int LABS_RP_DID_SIZE = 32;
        public const int LABS_SESSION_ID_SIZE_MAX = 80;
        public const int LABS_HANDSHAKE_KEY_SIZE = 0x10;
        public const int LABS_CONTROLLER_TOUCHES_MAX = 2;
        public const int LABS_FEEDBACK_STATE_BUF_SIZE_V9 = 0x19;
        public const int LABS_FEEDBACK_STATE_BUF_SIZE_V12 = 0x1c;
        public const int LABS_HISTORY_EVENT_SIZE_MAX = 0x5;
        public const int LABS_AUDIO_HEADER_SIZE = 0xe;
        public const int LABS_VIDEO_BUFFER_PADDING_SIZE = 64;

        public const int LABS_DISCOVERY_PORT_PS4 = 987;
        public const int LABS_DISCOVERY_PORT_PS5 = 9302;

        // Upper-bound size for LabsSession opaque buffer (session.h defines a
        // big struct; real sizeof must be obtained at runtime via a C helper.
        // 64 KiB is safe for every platform the upstream library targets).
        public const int LabsSessionSizeMax = 0x10000;

        // ------------------------------------------------------------------
        // Enums — common.h
        // ------------------------------------------------------------------

        /// LabsErrorCode (common.h)
        public enum LabsErrorCode : int
        {
            LABS_ERR_SUCCESS = 0,
            LABS_ERR_UNKNOWN,
            LABS_ERR_PARSE_ADDR,
            LABS_ERR_THREAD,
            LABS_ERR_MEMORY,
            LABS_ERR_OVERFLOW,
            LABS_ERR_NETWORK,
            LABS_ERR_CONNECTION_REFUSED,
            LABS_ERR_HOST_DOWN,
            LABS_ERR_HOST_UNREACH,
            LABS_ERR_DISCONNECTED,
            LABS_ERR_INVALID_DATA,
            LABS_ERR_BUF_TOO_SMALL,
            LABS_ERR_MUTEX_LOCKED,
            LABS_ERR_CANCELED,
            LABS_ERR_TIMEOUT,
            LABS_ERR_INVALID_RESPONSE,
            LABS_ERR_INVALID_MAC,
            LABS_ERR_UNINITIALIZED,
            LABS_ERR_FEC_FAILED,
            LABS_ERR_VERSION_MISMATCH,
            LABS_ERR_HTTP_NONOK
        }

        /// LabsTarget (common.h) — explicit integer values
        public enum LabsTarget : int
        {
            LABS_TARGET_PS4_UNKNOWN = 0,
            LABS_TARGET_PS4_8 = 800,
            LABS_TARGET_PS4_9 = 900,
            LABS_TARGET_PS4_10 = 1000,
            LABS_TARGET_PS5_UNKNOWN = 1000000,
            LABS_TARGET_PS5_1 = 1000100
        }

        /// LabsCodec (common.h)
        public enum LabsCodec : int
        {
            LABS_CODEC_H264 = 0,
            LABS_CODEC_H265 = 1,
            LABS_CODEC_H265_HDR = 2
        }

        /// LabsLogLevel (log.h) — bitmask
        [Flags]
        public enum LabsLogLevel : uint
        {
            LABS_LOG_ERROR = 1u << 0,
            LABS_LOG_WARNING = 1u << 1,
            LABS_LOG_INFO = 1u << 2,
            LABS_LOG_VERBOSE = 1u << 3,
            LABS_LOG_DEBUG = 1u << 4,
            LABS_LOG_ALL = (1u << 5) - 1
        }

        /// LabsDisableAudioVideo (takion.h)
        public enum LabsDisableAudioVideo : int
        {
            LABS_NONE_DISABLED = 0,
            LABS_AUDIO_DISABLED = 1,
            LABS_VIDEO_DISABLED = 2,
            LABS_AUDIO_VIDEO_DISABLED = 3
        }

        /// LabsDualSenseEffectIntensity (streamconnection.h)
        public enum LabsDualSenseEffectIntensity : int
        {
            Off = 0,
            Weak = 3,
            Medium = 2,
            Strong = 1
        }

        /// LabsDiscoveryCmd (discovery.h)
        public enum LabsDiscoveryCmd : int
        {
            LABS_DISCOVERY_CMD_SRCH,
            LABS_DISCOVERY_CMD_WAKEUP
        }

        /// LabsDiscoveryHostState (discovery.h)
        public enum LabsDiscoveryHostState : int
        {
            LABS_DISCOVERY_HOST_STATE_UNKNOWN,
            LABS_DISCOVERY_HOST_STATE_READY,
            LABS_DISCOVERY_HOST_STATE_STANDBY
        }

        /// LabsRegistEventType (regist.h)
        public enum LabsRegistEventType : int
        {
            LABS_REGIST_EVENT_TYPE_FINISHED_CANCELED,
            LABS_REGIST_EVENT_TYPE_FINISHED_FAILED,
            LABS_REGIST_EVENT_TYPE_FINISHED_SUCCESS
        }

        /// LabsQuitReason (session.h)
        public enum LabsQuitReason : int
        {
            LABS_QUIT_REASON_NONE,
            LABS_QUIT_REASON_STOPPED,
            LABS_QUIT_REASON_SESSION_REQUEST_UNKNOWN,
            LABS_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED,
            LABS_QUIT_REASON_SESSION_REQUEST_RP_IN_USE,
            LABS_QUIT_REASON_SESSION_REQUEST_RP_CRASH,
            LABS_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH,
            LABS_QUIT_REASON_CTRL_UNKNOWN,
            LABS_QUIT_REASON_CTRL_CONNECT_FAILED,
            LABS_QUIT_REASON_CTRL_CONNECTION_REFUSED,
            LABS_QUIT_REASON_STREAM_CONNECTION_UNKNOWN,
            LABS_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED,
            LABS_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN,
            LABS_QUIT_REASON_PSN_REGIST_FAILED
        }

        /// LabsEventType (session.h)
        public enum LabsEventType : int
        {
            LABS_EVENT_CONNECTED,
            LABS_EVENT_LOGIN_PIN_REQUEST,
            LABS_EVENT_HOLEPUNCH,
            LABS_EVENT_REGIST,
            LABS_EVENT_NICKNAME_RECEIVED,
            LABS_EVENT_KEYBOARD_OPEN,
            LABS_EVENT_KEYBOARD_TEXT_CHANGE,
            LABS_EVENT_KEYBOARD_REMOTE_CLOSE,
            LABS_EVENT_RUMBLE,
            LABS_EVENT_QUIT,
            LABS_EVENT_TRIGGER_EFFECTS,
            LABS_EVENT_MOTION_RESET,
            LABS_EVENT_LED_COLOR,
            LABS_EVENT_PLAYER_INDEX,
            LABS_EVENT_HAPTIC_INTENSITY,
            LABS_EVENT_TRIGGER_INTENSITY,
            LABS_EVENT_VIDEO_FEC_FAILURE
        }

        /// LabsVideoResolutionPreset (session.h)
        public enum LabsVideoResolutionPreset : int
        {
            LABS_VIDEO_RESOLUTION_PRESET_360p = 1,
            LABS_VIDEO_RESOLUTION_PRESET_540p = 2,
            LABS_VIDEO_RESOLUTION_PRESET_720p = 3,
            LABS_VIDEO_RESOLUTION_PRESET_1080p = 4
        }

        /// LabsVideoFPSPreset (session.h)
        public enum LabsVideoFPSPreset : int
        {
            LABS_VIDEO_FPS_PRESET_30 = 30,
            LABS_VIDEO_FPS_PRESET_60 = 60
        }

        /// LabsControllerButton (controller.h) — bitmask on LabsControllerState.buttons
        [Flags]
        public enum LabsControllerButton : uint
        {
            CROSS = 1u << 0,
            MOON = 1u << 1,
            BOX = 1u << 2,
            PYRAMID = 1u << 3,
            DPAD_LEFT = 1u << 4,
            DPAD_RIGHT = 1u << 5,
            DPAD_UP = 1u << 6,
            DPAD_DOWN = 1u << 7,
            L1 = 1u << 8,
            R1 = 1u << 9,
            L3 = 1u << 10,
            R3 = 1u << 11,
            OPTIONS = 1u << 12,
            SHARE = 1u << 13,
            TOUCHPAD = 1u << 14,
            PS = 1u << 15,
            // analog pseudo-buttons (feedback history only)
            L2 = 1u << 16,
            R2 = 1u << 17
        }

        // ------------------------------------------------------------------
        // Delegates (callbacks)
        // ------------------------------------------------------------------

        /// LabsLogCb (log.h)
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LabsLogCb(LabsLogLevel level, IntPtr msg, IntPtr user);

        /// LabsDiscoveryCb (discovery.h) — invoked for each host found
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LabsDiscoveryCb(IntPtr host /* LabsDiscoveryHost* */, IntPtr user);

        /// LabsDiscoveryServiceCb (discoveryservice.h)
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LabsDiscoveryServiceCb(IntPtr hosts /* LabsDiscoveryHost* */, UIntPtr hosts_count, IntPtr user);

        /// LabsRegistCb (regist.h)
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LabsRegistCb(IntPtr regist_event /* LabsRegistEvent* */, IntPtr user);

        /// LabsEventCallback (session.h) — events marshaled as IntPtr because LabsEvent uses a C union
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LabsEventCallback(IntPtr labs_event /* LabsEvent* */, IntPtr user);

        /// LabsVideoSampleCallback (session.h) — returns true if sample was pushed
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public delegate bool LabsVideoSampleCallback(IntPtr buf, UIntPtr buf_size, int frames_lost,
            [MarshalAs(UnmanagedType.I1)] bool frame_recovered, IntPtr user);

        /// LabsAudioSinkHeader (audioreceiver.h)
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LabsAudioSinkHeader(IntPtr header /* LabsAudioHeader* */, IntPtr user);

        /// LabsAudioSinkFrame (audioreceiver.h)
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LabsAudioSinkFrame(IntPtr buf, UIntPtr buf_size, IntPtr user);

        // ------------------------------------------------------------------
        // Structs — log.h
        // ------------------------------------------------------------------

        /// LabsLog (log.h) — caller allocates, initializes via labs_log_init
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsLog
        {
            public uint level_mask;
            public IntPtr cb;   // LabsLogCb
            public IntPtr user;
        }

        // ------------------------------------------------------------------
        // Structs — common.h / audio.h / video.h
        // ------------------------------------------------------------------

        /// LabsAudioHeader (audio.h)
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsAudioHeader
        {
            public byte channels;
            public byte bits;
            public uint rate;
            public uint frame_size;
            public uint unknown;
        }

        /// LabsAudioSink (audioreceiver.h)
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsAudioSink
        {
            public IntPtr user;
            public IntPtr header_cb; // LabsAudioSinkHeader
            public IntPtr frame_cb;  // LabsAudioSinkFrame
        }

        // ------------------------------------------------------------------
        // Structs — discovery.h
        // ------------------------------------------------------------------

        /// LabsDiscoveryHost (discovery.h) — all string fields are const char* (UTF-8)
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsDiscoveryHost
        {
            public LabsDiscoveryHostState state;
            public ushort host_request_port;
            public IntPtr host_addr;
            public IntPtr system_version;
            public IntPtr device_discovery_protocol_version;
            public IntPtr host_name;
            public IntPtr host_type;
            public IntPtr host_id;
            public IntPtr running_app_titleid;
            public IntPtr running_app_name;
        }

        /// LabsDiscoveryPacket (discovery.h)
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsDiscoveryPacket
        {
            public LabsDiscoveryCmd cmd;
            public IntPtr protocol_version; // char*
            public ulong user_credential;
        }

        // ------------------------------------------------------------------
        // Structs — regist.h
        // ------------------------------------------------------------------

        /// LabsRegistInfo (regist.h) — holepunch_info/rudp omitted (null for LAN regist).
        /// Note: LabsRudp is an embedded struct, so we use a generous reserved block
        /// whose exact size must match the native header; for LAN pairing we leave
        /// it zero-initialized. Callers wanting PSN regist must extend this.
        [StructLayout(LayoutKind.Sequential)]
        public unsafe struct LabsRegistInfo
        {
            public LabsTarget target;
            public IntPtr host;                // const char*
            [MarshalAs(UnmanagedType.I1)] public bool broadcast;
            public IntPtr psn_online_id;       // const char* (may be null)
            public fixed byte psn_account_id[LABS_PSN_ACCOUNT_ID_SIZE];
            public uint pin;
            public uint console_pin;
            public IntPtr holepunch_info;      // LabsHolepunchRegistInfo* (null for LAN)
            public IntPtr rudp;                // LabsRudp = struct rudp_t* (null for LAN)
        }

        /// LabsRegisteredHost (regist.h) — written by the library on successful regist
        [StructLayout(LayoutKind.Sequential)]
        public unsafe struct LabsRegisteredHost
        {
            public LabsTarget target;
            public fixed byte ap_ssid[0x30];
            public fixed byte ap_bssid[0x20];
            public fixed byte ap_key[0x50];
            public fixed byte ap_name[0x20];
            public fixed byte server_mac[6];
            public fixed byte server_nickname[0x20];
            public fixed byte rp_regist_key[LABS_SESSION_AUTH_SIZE];
            public uint rp_key_type;
            public fixed byte rp_key[0x10];
            public uint console_pin;
        }

        /// LabsRegistEvent (regist.h)
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsRegistEvent
        {
            public LabsRegistEventType type;
            public IntPtr registered_host; // LabsRegisteredHost*
        }

        // ------------------------------------------------------------------
        // Structs — session.h
        // ------------------------------------------------------------------

        /// LabsConnectVideoProfile (session.h)
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsConnectVideoProfile
        {
            public uint width;
            public uint height;
            public uint max_fps;
            public uint bitrate;
            public LabsCodec codec;
        }

        /// LabsConnectInfo (session.h) — holepunch_session/rudp_sock left zero for LAN play.
        /// IMPORTANT: in C, bool is 1 byte — fields are mapped to byte + manual conversion
        /// helpers below to avoid default bool marshalling (4 bytes).
        [StructLayout(LayoutKind.Sequential)]
        public unsafe struct LabsConnectInfo
        {
            public byte ps5;                 // bool
            public IntPtr host;              // const char*
            public fixed byte regist_key[LABS_SESSION_AUTH_SIZE];
            public fixed byte morning[0x10];
            public LabsConnectVideoProfile video_profile;
            public byte video_profile_auto_downgrade; // bool
            public byte enable_keyboard;              // bool
            public byte enable_dualsense;             // bool
            public LabsDisableAudioVideo audio_video_disabled;
            public byte auto_regist;                  // bool
            public IntPtr holepunch_session;          // LabsHolepunchSession = struct session_t* (null for LAN)
            public IntPtr rudp_sock;                  // labs_socket_t* (null for LAN)
            public fixed byte psn_account_id[LABS_PSN_ACCOUNT_ID_SIZE];
            public double packet_loss_max;
            public byte enable_idr_on_fec_failure;    // bool
        }

        /// LabsQuitEvent (session.h)
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsQuitEvent
        {
            public LabsQuitReason reason;
            public IntPtr reason_str;
        }

        /// LabsKeyboardEvent (session.h)
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsKeyboardEvent
        {
            public IntPtr text_str;
        }

        /// LabsRumbleEvent (session.h)
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsRumbleEvent
        {
            public byte unknown;
            public byte left;
            public byte right;
        }

        /// LabsTriggerEffectsEvent (session.h)
        [StructLayout(LayoutKind.Sequential)]
        public unsafe struct LabsTriggerEffectsEvent
        {
            public byte type_left;
            public byte type_right;
            public fixed byte left[10];
            public fixed byte right[10];
        }

        /// LabsVideoFecFailureEvent (session.h)
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsVideoFecFailureEvent
        {
            public int frame_index;
            [MarshalAs(UnmanagedType.I1)] public bool idr_request_sent;
        }

        // LabsEvent uses a C union — we expose only the discriminator here.
        // Consumers should read the type then dereference the `event+4` payload
        // using the appropriate struct (above). A helper offset constant:
        public const int LabsEventPayloadOffset = 4; // sizeof(LabsEventType)

        // ------------------------------------------------------------------
        // Structs — controller.h / feedback.h
        // ------------------------------------------------------------------

        /// LabsControllerTouch (controller.h)
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsControllerTouch
        {
            public ushort x;
            public ushort y;
            public sbyte id; // -1 = up
        }

        /// LabsControllerState (controller.h)
        [StructLayout(LayoutKind.Sequential)]
        public unsafe struct LabsControllerState
        {
            public uint buttons;
            public byte l2_state;
            public byte r2_state;
            public short left_x;
            public short left_y;
            public short right_x;
            public short right_y;
            public byte touch_id_next;
            public LabsControllerTouch touch0;
            public LabsControllerTouch touch1;
            public float gyro_x, gyro_y, gyro_z;
            public float accel_x, accel_y, accel_z;
            public float orient_x, orient_y, orient_z, orient_w;
        }

        /// LabsFeedbackState (feedback.h)
        [StructLayout(LayoutKind.Sequential)]
        public struct LabsFeedbackState
        {
            public float gyro_x, gyro_y, gyro_z;
            public float accel_x, accel_y, accel_z;
            public float orient_x, orient_y, orient_z, orient_w;
            public short left_x, left_y, right_x, right_y;
        }

        /// LabsFeedbackHistoryEvent (feedback.h)
        [StructLayout(LayoutKind.Sequential)]
        public unsafe struct LabsFeedbackHistoryEvent
        {
            public fixed byte buf[LABS_HISTORY_EVENT_SIZE_MAX];
            public UIntPtr len;
        }

        // ==================================================================
        //  P / I N V O K E S
        // ==================================================================

        // ---------- common.h ----------

        /// labs_lib_init — initializes global library state (call once).
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_lib_init")]
        public static extern LabsErrorCode labs_lib_init();

        /// labs_error_string — returns a static UTF-8 string for the given code.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_error_string")]
        public static extern IntPtr labs_error_string(LabsErrorCode code);

        /// labs_codec_name — returns a static UTF-8 codec name.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_codec_name")]
        public static extern IntPtr labs_codec_name(LabsCodec codec);

        /// labs_aligned_alloc — allocate aligned memory (freed with labs_aligned_free).
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_aligned_alloc")]
        public static extern IntPtr labs_aligned_alloc(UIntPtr alignment, UIntPtr size);

        /// labs_aligned_free — free memory allocated by labs_aligned_alloc.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_aligned_free")]
        public static extern void labs_aligned_free(IntPtr ptr);

        // ---------- log.h ----------

        /// labs_log_init — initialize a LabsLog struct with level mask and callback.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_log_init")]
        public static extern void labs_log_init(ref LabsLog log, uint level_mask, IntPtr cb, IntPtr user);

        /// labs_log_set_level — update level mask of an existing LabsLog.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_log_set_level")]
        public static extern void labs_log_set_level(ref LabsLog log, uint level_mask);

        /// labs_log_cb_print — built-in log callback that prints to stdout.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_log_cb_print")]
        public static extern void labs_log_cb_print(LabsLogLevel level, IntPtr msg, IntPtr user);

        // ---------- discovery.h ----------

        /// labs_discovery_host_state_string — returns UTF-8 state name.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_discovery_host_state_string")]
        public static extern IntPtr labs_discovery_host_state_string(LabsDiscoveryHostState state);

        /// labs_discovery_host_is_ps5 — true if host struct describes a PS5.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_discovery_host_is_ps5")]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool labs_discovery_host_is_ps5(ref LabsDiscoveryHost host);

        /// labs_discovery_host_system_version_target — parse firmware version into LabsTarget.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_discovery_host_system_version_target")]
        public static extern LabsTarget labs_discovery_host_system_version_target(ref LabsDiscoveryHost host);

        /// labs_discovery_init — initialize a discovery socket (caller-allocated LabsDiscovery buffer).
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_discovery_init")]
        public static extern LabsErrorCode labs_discovery_init(IntPtr discovery, ref LabsLog log, ushort family);

        /// labs_discovery_fini — close and free discovery resources.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_discovery_fini")]
        public static extern void labs_discovery_fini(IntPtr discovery);

        /// labs_discovery_thread_start — begin async discovery loop.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_discovery_thread_start")]
        public static extern LabsErrorCode labs_discovery_thread_start(IntPtr thread, IntPtr discovery, IntPtr cb, IntPtr cb_user);

        /// labs_discovery_thread_start_oneshot — discovery loop that stops after one round.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_discovery_thread_start_oneshot")]
        public static extern LabsErrorCode labs_discovery_thread_start_oneshot(IntPtr thread, IntPtr discovery, IntPtr cb, IntPtr cb_user);

        /// labs_discovery_thread_stop — signal and join the discovery thread.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_discovery_thread_stop")]
        public static extern LabsErrorCode labs_discovery_thread_stop(IntPtr thread);

        /// labs_discovery_wakeup — send wake-on-LAN-style packet to power on a console.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_discovery_wakeup")]
        public static extern LabsErrorCode labs_discovery_wakeup(ref LabsLog log, IntPtr discovery,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string host, ulong user_credential,
            [MarshalAs(UnmanagedType.I1)] bool ps5);

        // ---------- discoveryservice.h ----------

        /// labs_discovery_service_init — start a managed discovery service with options.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_discovery_service_init")]
        public static extern LabsErrorCode labs_discovery_service_init(IntPtr service, IntPtr options, ref LabsLog log);

        /// labs_discovery_service_fini — stop and free discovery service.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_discovery_service_fini")]
        public static extern void labs_discovery_service_fini(IntPtr service);

        // ---------- regist.h ----------

        /// labs_regist_start — begin async registration with a console.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_regist_start")]
        public static extern LabsErrorCode labs_regist_start(IntPtr regist, ref LabsLog log,
            ref LabsRegistInfo info, IntPtr cb, IntPtr cb_user);

        /// labs_regist_fini — release registration resources after completion.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_regist_fini")]
        public static extern void labs_regist_fini(IntPtr regist);

        /// labs_regist_stop — cancel an in-progress registration.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_regist_stop")]
        public static extern void labs_regist_stop(IntPtr regist);

        // ---------- session.h ----------

        /// labs_rp_application_reason_string — translates 0x8010xxxx reason to UTF-8 text.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_rp_application_reason_string")]
        public static extern IntPtr labs_rp_application_reason_string(uint reason);

        /// labs_rp_version_string — "RP-Version" string for target, or null.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_rp_version_string")]
        public static extern IntPtr labs_rp_version_string(LabsTarget target);

        /// labs_connect_video_profile_preset — fills LabsConnectVideoProfile from presets.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_connect_video_profile_preset")]
        public static extern void labs_connect_video_profile_preset(ref LabsConnectVideoProfile profile,
            LabsVideoResolutionPreset resolution, LabsVideoFPSPreset fps);

        /// labs_quit_reason_string — UTF-8 description of LabsQuitReason.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_quit_reason_string")]
        public static extern IntPtr labs_quit_reason_string(LabsQuitReason reason);

        /// labs_session_init — initialize a caller-allocated LabsSession.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_init")]
        public static extern LabsErrorCode labs_session_init(IntPtr session, ref LabsConnectInfo connect_info, ref LabsLog log);

        /// labs_session_fini — release session resources.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_fini")]
        public static extern void labs_session_fini(IntPtr session);

        /// labs_session_start — begin the session thread (connect + stream).
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_start")]
        public static extern LabsErrorCode labs_session_start(IntPtr session);

        /// labs_session_stop — signal the session to stop.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_stop")]
        public static extern LabsErrorCode labs_session_stop(IntPtr session);

        /// labs_session_join — wait for the session thread to exit.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_join")]
        public static extern LabsErrorCode labs_session_join(IntPtr session);

        /// labs_session_request_idr — ask the console for a new keyframe.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_request_idr")]
        public static extern LabsErrorCode labs_session_request_idr(IntPtr session);

        /// labs_session_set_controller_state — send a full controller state snapshot.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_set_controller_state")]
        public static extern LabsErrorCode labs_session_set_controller_state(IntPtr session, ref LabsControllerState state);

        /// labs_session_set_login_pin — send the 8-digit login PIN after LOGIN_PIN_REQUEST.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_set_login_pin")]
        public static extern LabsErrorCode labs_session_set_login_pin(IntPtr session, byte[] pin, UIntPtr pin_size);

        /// labs_session_goto_bed — request the console to enter rest mode.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_goto_bed")]
        public static extern LabsErrorCode labs_session_goto_bed(IntPtr session);

        /// labs_session_toggle_microphone — mute/unmute the virtual mic.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_toggle_microphone")]
        public static extern LabsErrorCode labs_session_toggle_microphone(IntPtr session,
            [MarshalAs(UnmanagedType.I1)] bool muted);

        /// labs_session_connect_microphone — begin streaming local microphone to console.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_connect_microphone")]
        public static extern LabsErrorCode labs_session_connect_microphone(IntPtr session);

        /// labs_session_keyboard_set_text — set remote on-screen keyboard text.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_keyboard_set_text")]
        public static extern LabsErrorCode labs_session_keyboard_set_text(IntPtr session,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string text);

        /// labs_session_keyboard_reject — cancel on-screen keyboard input.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_keyboard_reject")]
        public static extern LabsErrorCode labs_session_keyboard_reject(IntPtr session);

        /// labs_session_keyboard_accept — commit on-screen keyboard input.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_keyboard_accept")]
        public static extern LabsErrorCode labs_session_keyboard_accept(IntPtr session);

        /// labs_session_go_home — emulate pressing the PS "home" shortcut.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_go_home")]
        public static extern LabsErrorCode labs_session_go_home(IntPtr session);

        // NOTE: labs_session_set_event_cb/video_sample_cb/audio_sink/haptics_sink
        // are `static inline` helpers in session.h, so they are NOT exported.
        // Managed callers must set those fields directly by writing into the
        // LabsSession memory, or by using a tiny C shim. The field offsets vary
        // by build and are NOT stable, so use a shim.

        // ---------- controller.h ----------

        /// labs_controller_state_set_idle — zero out a controller state (neutral).
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_controller_state_set_idle")]
        public static extern void labs_controller_state_set_idle(ref LabsControllerState state);

        /// labs_controller_state_start_touch — allocate a touch slot, returns id or -1.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_controller_state_start_touch")]
        public static extern sbyte labs_controller_state_start_touch(ref LabsControllerState state, ushort x, ushort y);

        /// labs_controller_state_stop_touch — release a touch slot by id.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_controller_state_stop_touch")]
        public static extern void labs_controller_state_stop_touch(ref LabsControllerState state, byte id);

        /// labs_controller_state_set_touch_pos — update coordinates of an existing touch.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_controller_state_set_touch_pos")]
        public static extern void labs_controller_state_set_touch_pos(ref LabsControllerState state, byte id, ushort x, ushort y);

        /// labs_controller_state_equals — compare two controller states.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_controller_state_equals")]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool labs_controller_state_equals(ref LabsControllerState a, ref LabsControllerState b);

        /// labs_controller_state_or — OR-combine two controller states into out.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_controller_state_or")]
        public static extern void labs_controller_state_or(ref LabsControllerState @out,
            ref LabsControllerState a, ref LabsControllerState b);

        // ---------- feedback.h ----------

        /// labs_feedback_state_format_v9 — serialize legacy feedback state.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_feedback_state_format_v9")]
        public static extern void labs_feedback_state_format_v9(byte[] buf, ref LabsFeedbackState state);

        /// labs_feedback_state_format_v12 — serialize v12 feedback state.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_feedback_state_format_v12")]
        public static extern void labs_feedback_state_format_v12(byte[] buf, ref LabsFeedbackState state);

        /// labs_feedback_history_event_set_button — mark a button event (analog value).
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_feedback_history_event_set_button")]
        public static extern LabsErrorCode labs_feedback_history_event_set_button(
            ref LabsFeedbackHistoryEvent ev, ulong button, byte state);

        /// labs_feedback_history_event_set_touchpad — encode a touchpad event.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_feedback_history_event_set_touchpad")]
        public static extern void labs_feedback_history_event_set_touchpad(ref LabsFeedbackHistoryEvent ev,
            [MarshalAs(UnmanagedType.I1)] bool down, byte pointer_id, ushort x, ushort y);

        // ---------- audio.h ----------

        /// labs_audio_header_set — fill an audio header struct.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_audio_header_set")]
        public static extern void labs_audio_header_set(ref LabsAudioHeader header, byte channels, byte bits, uint rate, uint frame_size);

        /// labs_audio_header_load — parse wire-format audio header.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_audio_header_load")]
        public static extern void labs_audio_header_load(ref LabsAudioHeader header, byte[] buf);

        /// labs_audio_header_save — serialize audio header to LABS_AUDIO_HEADER_SIZE bytes.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_audio_header_save")]
        public static extern void labs_audio_header_save(ref LabsAudioHeader header, byte[] buf);

        // ------------------------------------------------------------------
        //  session_shims.c — exported wrappers for session.h static inlines
        // ------------------------------------------------------------------

        /// labs_session_sizeof — returns sizeof(LabsSession) so managed code
        /// can allocate a correctly-sized opaque buffer.
        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_sizeof")]
        public static extern UIntPtr labs_session_sizeof();

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_set_event_cb_ex")]
        public static extern void labs_session_set_event_cb(IntPtr session, IntPtr cb, IntPtr user);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_set_video_sample_cb_ex")]
        public static extern void labs_session_set_video_sample_cb(IntPtr session, IntPtr cb, IntPtr user);

        [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "labs_session_set_audio_sink_ex")]
        public static extern void labs_session_set_audio_sink(IntPtr session, ref LabsAudioSink sink);

        // ==================================================================
        //  H E L P E R S
        // ==================================================================

        /// Marshal a native UTF-8 `const char *` (as returned by labs_error_string etc.) to string.
        public static string? PtrToStringUTF8(IntPtr p)
        {
            if (p == IntPtr.Zero) return null;
            return Marshal.PtrToStringUTF8(p);
        }
    }
}
