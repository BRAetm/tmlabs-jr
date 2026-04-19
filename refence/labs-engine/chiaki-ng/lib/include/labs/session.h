// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_SESSION_H
#define LABS_SESSION_H

#include "streamconnection.h"
#include "common.h"
#include "thread.h"
#include "log.h"
#include "ctrl.h"
#include "rpcrypt.h"
#include "takion.h"
#include "ecdh.h"
#include "audio.h"
#include "controller.h"
#include "stoppipe.h"
#include "remote/holepunch.h"
#include "remote/rudp.h"
#include "regist.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LABS_RP_APPLICATION_REASON_REGIST_FAILED		0x80108b09
#define LABS_RP_APPLICATION_REASON_INVALID_PSN_ID		0x80108b02
#define LABS_RP_APPLICATION_REASON_IN_USE				0x80108b10
#define LABS_RP_APPLICATION_REASON_CRASH				0x80108b15
#define LABS_RP_APPLICATION_REASON_RP_VERSION			0x80108b11
#define LABS_RP_APPLICATION_REASON_UNKNOWN			0x80108bff

LABS_EXPORT const char *labs_rp_application_reason_string(uint32_t reason);

/**
 * @return RP-Version string or NULL
 */
LABS_EXPORT const char *labs_rp_version_string(LabsTarget target);

LABS_EXPORT LabsTarget labs_rp_version_parse(const char *rp_version_str, bool is_ps5);


#define LABS_RP_DID_SIZE 32
#define LABS_SESSION_ID_SIZE_MAX 80
#define LABS_HANDSHAKE_KEY_SIZE 0x10

typedef struct labs_connect_video_profile_t
{
	unsigned int width;
	unsigned int height;
	unsigned int max_fps;
	unsigned int bitrate;
	LabsCodec codec;
} LabsConnectVideoProfile;

typedef enum {
	// values must not change
	LABS_VIDEO_RESOLUTION_PRESET_360p = 1,
	LABS_VIDEO_RESOLUTION_PRESET_540p = 2,
	LABS_VIDEO_RESOLUTION_PRESET_720p = 3,
	LABS_VIDEO_RESOLUTION_PRESET_1080p = 4
} LabsVideoResolutionPreset;

typedef enum {
	// values must not change
	LABS_VIDEO_FPS_PRESET_30 = 30,
	LABS_VIDEO_FPS_PRESET_60 = 60
} LabsVideoFPSPreset;

LABS_EXPORT void labs_connect_video_profile_preset(LabsConnectVideoProfile *profile, LabsVideoResolutionPreset resolution, LabsVideoFPSPreset fps);

#define LABS_SESSION_AUTH_SIZE 0x10

typedef struct labs_connect_info_t
{
	bool ps5;
	const char *host; // null terminated
	char regist_key[LABS_SESSION_AUTH_SIZE]; // must be completely filled (pad with \0)
	uint8_t morning[0x10];
	LabsConnectVideoProfile video_profile;
	bool video_profile_auto_downgrade; // Downgrade video_profile if server does not seem to support it.
	bool enable_keyboard;
	bool enable_dualsense;
	LabsDisableAudioVideo audio_video_disabled;
	bool auto_regist;
	LabsHolepunchSession holepunch_session;
	labs_socket_t *rudp_sock;
	uint8_t psn_account_id[LABS_PSN_ACCOUNT_ID_SIZE];
	double packet_loss_max;
	bool enable_idr_on_fec_failure;
} LabsConnectInfo;


typedef enum {
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
	LABS_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN, // like REMOTE_DISCONNECTED, but because the server shut down
	LABS_QUIT_REASON_PSN_REGIST_FAILED,
} LabsQuitReason;

LABS_EXPORT const char *labs_quit_reason_string(LabsQuitReason reason);

static inline bool labs_quit_reason_is_error(LabsQuitReason reason)
{
	return reason != LABS_QUIT_REASON_STOPPED && reason != LABS_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN;
}

typedef struct labs_quit_event_t
{
	LabsQuitReason reason;
	const char *reason_str;
} LabsQuitEvent;

typedef struct labs_keyboard_event_t
{
	const char *text_str;
} LabsKeyboardEvent;

typedef struct labs_audio_stream_info_event_t
{
	LabsAudioHeader audio_header;
} LabsAudioStreamInfoEvent;

typedef struct labs_rumble_event_t
{
	uint8_t unknown;
	uint8_t left; // low-frequency
	uint8_t right; // high-frequency
} LabsRumbleEvent;

typedef struct labs_trigger_effects_event_t
{
	uint8_t type_left;
	uint8_t type_right;
	uint8_t left[10];
	uint8_t right[10];
} LabsTriggerEffectsEvent;

typedef struct labs_video_fec_failure_event_t
{
	int32_t frame_index;
	bool idr_request_sent;
} LabsVideoFecFailureEvent;

typedef enum {
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
	LABS_EVENT_VIDEO_FEC_FAILURE,
} LabsEventType;

typedef struct labs_event_t
{
	LabsEventType type;
	union
	{
		LabsQuitEvent quit;
		LabsKeyboardEvent keyboard;
		LabsRumbleEvent rumble;
		LabsRegisteredHost host;
		LabsTriggerEffectsEvent trigger_effects;
		uint8_t led_state[0x3];
		uint8_t player_index;
		struct
		{
			bool pin_incorrect; // false on first request, true if the pin entered before was incorrect
		} login_pin_request;
		struct
		{
			bool finished; // false when punching hole, true when finished
		} data_holepunch;
		LabsDualSenseEffectIntensity intensity;
		char server_nickname[0x20];
		LabsVideoFecFailureEvent video_fec_failure;
	};
} LabsEvent;

typedef void (*LabsEventCallback)(LabsEvent *event, void *user);

/**
 * buf will always have an allocated padding of at least LABS_VIDEO_BUFFER_PADDING_SIZE after buf_size
 * @return whether the sample was successfully pushed into the decoder. On false, a corrupt frame will be reported to get a new keyframe.
 */
typedef bool (*LabsVideoSampleCallback)(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user);



typedef struct labs_session_t
{
	struct
	{
		bool ps5;
		struct addrinfo *host_addrinfos;
		struct addrinfo *host_addrinfo_selected;
		char hostname[256];
		char regist_key[LABS_RPCRYPT_KEY_SIZE];
		uint8_t morning[LABS_RPCRYPT_KEY_SIZE];
		uint8_t did[LABS_RP_DID_SIZE];
		LabsConnectVideoProfile video_profile;
		bool video_profile_auto_downgrade;
		LabsDisableAudioVideo disable_audio_video;
		bool enable_keyboard;
		bool enable_dualsense;
		uint8_t psn_account_id[LABS_PSN_ACCOUNT_ID_SIZE];
		bool enable_idr_on_fec_failure;
	} connect_info;

	LabsTarget target;

	uint8_t nonce[LABS_RPCRYPT_KEY_SIZE];
	LabsRPCrypt rpcrypt;
	char session_id[LABS_SESSION_ID_SIZE_MAX]; // zero-terminated
	uint8_t handshake_key[LABS_HANDSHAKE_KEY_SIZE];
	uint32_t mtu_in;
	uint32_t mtu_out;
	uint64_t rtt_us;
	bool dontfrag;
	LabsECDH ecdh;

	LabsQuitReason quit_reason;
	char *quit_reason_str; // additional reason string from remote

	LabsEventCallback event_cb;
	void *event_cb_user;
	LabsVideoSampleCallback video_sample_cb;
	void *video_sample_cb_user;
	LabsAudioSink audio_sink;
	LabsAudioSink haptics_sink;
	LabsCtrlDisplaySink display_sink;

	LabsThread session_thread;

	LabsCond state_cond;
	LabsMutex state_mutex;
	LabsStopPipe stop_pipe;
	bool auto_regist;
	bool should_stop;
	bool ctrl_failed;
	bool ctrl_session_id_received;
	bool ctrl_login_pin_requested;
	bool ctrl_first_heartbeat_received;
	bool login_pin_entered;
	bool psn_regist_succeeded;
	bool stream_connection_switch_received;
	uint8_t *login_pin;
	size_t login_pin_size;

	LabsCtrl ctrl;
	LabsHolepunchSession holepunch_session;
	LabsRudp rudp;

	LabsLog *log;

	LabsStreamConnection stream_connection;

	LabsControllerState controller_state;
} LabsSession;

LABS_EXPORT LabsErrorCode labs_session_init(LabsSession *session, LabsConnectInfo *connect_info, LabsLog *log);
LABS_EXPORT void labs_session_fini(LabsSession *session);
LABS_EXPORT LabsErrorCode labs_session_start(LabsSession *session);
LABS_EXPORT LabsErrorCode labs_session_stop(LabsSession *session);
LABS_EXPORT LabsErrorCode labs_session_join(LabsSession *session);

LABS_EXPORT void labs_session_send_event(LabsSession *session, LabsEvent *event);

LABS_EXPORT LabsErrorCode labs_session_request_idr(LabsSession *session);
LABS_EXPORT LabsErrorCode labs_session_set_controller_state(LabsSession *session, LabsControllerState *state);
LABS_EXPORT LabsErrorCode labs_session_set_login_pin(LabsSession *session, const uint8_t *pin, size_t pin_size);
LABS_EXPORT LabsErrorCode labs_session_set_stream_connection_switch_received(LabsSession *session);
LABS_EXPORT LabsErrorCode labs_session_goto_bed(LabsSession *session);
LABS_EXPORT LabsErrorCode labs_session_toggle_microphone(LabsSession *session, bool muted);
LABS_EXPORT LabsErrorCode labs_session_connect_microphone(LabsSession *session);
LABS_EXPORT LabsErrorCode labs_session_keyboard_set_text(LabsSession *session, const char *text);
LABS_EXPORT LabsErrorCode labs_session_keyboard_reject(LabsSession *session);
LABS_EXPORT LabsErrorCode labs_session_keyboard_accept(LabsSession *session);
LABS_EXPORT LabsErrorCode labs_session_go_home(LabsSession *session);

static inline void labs_session_set_event_cb(LabsSession *session, LabsEventCallback cb, void *user)
{
	session->event_cb = cb;
	session->event_cb_user = user;
}

static inline void labs_session_set_video_sample_cb(LabsSession *session, LabsVideoSampleCallback cb, void *user)
{
	session->video_sample_cb = cb;
	session->video_sample_cb_user = user;
}

/**
 * @param sink contents are copied
 */
static inline void labs_session_set_audio_sink(LabsSession *session, LabsAudioSink *sink)
{
	session->audio_sink = *sink;
}

/**
 * @param sink contents are copied
 */
static inline void labs_session_set_haptics_sink(LabsSession *session, LabsAudioSink *sink)
{
	session->haptics_sink = *sink;
}

/**
 * @param sink contents are copied
 */
static inline void labs_session_ctrl_set_display_sink(LabsSession *session, LabsCtrlDisplaySink *sink)
{
	session->display_sink = *sink;
}

#ifdef __cplusplus
}
#endif

#endif // LABS_SESSION_H
