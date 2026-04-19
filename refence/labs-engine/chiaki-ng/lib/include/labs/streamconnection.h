// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_STREAMCONNECTION_H
#define LABS_STREAMCONNECTION_H

#include "feedbacksender.h"
#include "takion.h"
#include "log.h"
#include "ecdh.h"
#include "gkcrypt.h"
#include "audioreceiver.h"
#include "videoreceiver.h"
#include "congestioncontrol.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_session_t LabsSession;

typedef enum labs_dualsense_effect_intensity_t
{
	Off = 0,
	Weak = 3,
	Medium = 2,
	Strong = 1,
} LabsDualSenseEffectIntensity;
typedef struct labs_stream_connection_t
{
	struct labs_session_t *session;
	LabsLog *log;
	LabsTakion takion;
	uint8_t *ecdh_secret;
	LabsGKCrypt *gkcrypt_local;
	LabsGKCrypt *gkcrypt_remote;
	uint8_t *streaminfo_early_buf;
	size_t streaminfo_early_buf_size;

	LabsPacketStats packet_stats;
	LabsAudioReceiver *audio_receiver;
	LabsVideoReceiver *video_receiver;
	LabsAudioReceiver *haptics_receiver;
	double packet_loss_max;
	uint8_t motion_counter[4];
	uint8_t led_state[3];
	uint8_t player_index;
	LabsDualSenseEffectIntensity haptic_intensity;
	LabsDualSenseEffectIntensity trigger_intensity;
	LabsFeedbackSender feedback_sender;
	LabsCongestionControl congestion_control;
	/**
	 * whether feedback_sender is initialized
	 * only if this is true, feedback_sender may be accessed!
	 */
	bool feedback_sender_active;
	/**
	 * protects feedback_sender and feedback_sender_active
	 */
	LabsMutex feedback_sender_mutex;

	/**
	 * signaled on change of state_finished or should_stop
	 */
	LabsCond state_cond;

	/**
	 * protects state, state_finished, state_failed and should_stop
	 */
	LabsMutex state_mutex;

	int state;
	bool state_finished;
	bool state_failed;
	bool should_stop;
	bool remote_disconnected;
	char *remote_disconnect_reason;

	double measured_bitrate;
} LabsStreamConnection;

LABS_EXPORT LabsErrorCode labs_stream_connection_init(LabsStreamConnection *stream_connection, LabsSession *session, double packet_loss_max);
LABS_EXPORT void labs_stream_connection_fini(LabsStreamConnection *stream_connection);

/**
 * Run stream_connection synchronously
 */
LABS_EXPORT LabsErrorCode labs_stream_connection_run(LabsStreamConnection *stream_connection, labs_socket_t *socket);

LABS_EXPORT LabsErrorCode stream_connection_send_toggle_mute_direct_message(LabsStreamConnection *stream_connection, bool muted);
/**
 * To be called from a thread other than the one labs_stream_connection_run() is running on to stop stream_connection
 */
LABS_EXPORT LabsErrorCode labs_stream_connection_stop(LabsStreamConnection *stream_connection);

LABS_EXPORT LabsErrorCode stream_connection_send_corrupt_frame(LabsStreamConnection *stream_connection, LabsSeqNum16 start, LabsSeqNum16 end);
LABS_EXPORT LabsErrorCode stream_connection_send_idr_request(LabsStreamConnection *stream_connection);

#ifdef __cplusplus
}
#endif

#endif //LABS_STREAMCONNECTION_H
