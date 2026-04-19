// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL


#include "labs/common.h"
#include <labs/streamconnection.h>
#include <labs/session.h>
#include <labs/launchspec.h>
#include <labs/base64.h>
#include <labs/audio.h>
#include <labs/video.h>

#include <string.h>
#include <inttypes.h>
#include <assert.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#include <takion.pb.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <pb.h>

#include "utils.h"
#include "pb_utils.h"


#define STREAM_CONNECTION_PORT 9296

#define EXPECT_TIMEOUT_MS 5000

#define HEARTBEAT_INTERVAL_MS 1000


typedef enum {
	STATE_IDLE,
	STATE_TAKION_CONNECT,
	STATE_EXPECT_BANG,
	STATE_EXPECT_STREAMINFO
} StreamConnectionState;

void labs_session_send_event(LabsSession *session, LabsEvent *event);

static void stream_connection_takion_cb(LabsTakionEvent *event, void *user);
static void stream_connection_takion_data(LabsStreamConnection *stream_connection, LabsTakionMessageDataType data_type, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_protobuf(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_rumble(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_pad_info(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_trigger_effects(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static LabsErrorCode stream_connection_send_big(LabsStreamConnection *stream_connection);
static LabsErrorCode stream_connection_send_controller_connection(LabsStreamConnection *stream_connection);
static LabsErrorCode stream_connection_enable_microphone(LabsStreamConnection *stream_connection);
static LabsErrorCode stream_connection_send_disconnect(LabsStreamConnection *stream_connection);
static void stream_connection_takion_data_idle(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_expect_bang(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static void stream_connection_takion_data_expect_streaminfo(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size);
static LabsErrorCode stream_connection_send_streaminfo_ack(LabsStreamConnection *stream_connection);
static void stream_connection_takion_av(LabsStreamConnection *stream_connection, LabsTakionAVPacket *packet);
static LabsErrorCode stream_connection_send_heartbeat(LabsStreamConnection *stream_connection);

LABS_EXPORT LabsErrorCode labs_stream_connection_init(LabsStreamConnection *stream_connection, LabsSession *session, double packet_loss_max)
{
	stream_connection->session = session;
	stream_connection->log = session->log;
	stream_connection->packet_loss_max = packet_loss_max;

	stream_connection->ecdh_secret = NULL;
	stream_connection->gkcrypt_remote = NULL;
	stream_connection->gkcrypt_local = NULL;
	stream_connection->streaminfo_early_buf = NULL;
	stream_connection->streaminfo_early_buf_size = 0;
	stream_connection->player_index = 0;
	memset(stream_connection->led_state, 0, sizeof(stream_connection->led_state));

	stream_connection->haptic_intensity = Strong;
	stream_connection->trigger_intensity = Strong;

	LabsErrorCode err = labs_mutex_init(&stream_connection->state_mutex, false);
	if(err != LABS_ERR_SUCCESS)
		goto error;

	err = labs_cond_init(&stream_connection->state_cond);
	if(err != LABS_ERR_SUCCESS)
		goto error_state_mutex;

	err = labs_packet_stats_init(&stream_connection->packet_stats);
	if(err != LABS_ERR_SUCCESS)
		goto error_state_cond;

	stream_connection->video_receiver = NULL;
	stream_connection->audio_receiver = NULL;
	stream_connection->haptics_receiver = NULL;

	err = labs_mutex_init(&stream_connection->feedback_sender_mutex, false);
	if(err != LABS_ERR_SUCCESS)
		goto error_packet_stats;

	stream_connection->state = STATE_IDLE;
	stream_connection->state_finished = false;
	stream_connection->state_failed = false;
	stream_connection->should_stop = false;
	stream_connection->remote_disconnected = false;
	stream_connection->remote_disconnect_reason = NULL;

	return LABS_ERR_SUCCESS;

error_packet_stats:
	labs_packet_stats_fini(&stream_connection->packet_stats);
error_state_cond:
	labs_cond_fini(&stream_connection->state_cond);
error_state_mutex:
	labs_mutex_fini(&stream_connection->state_mutex);
error:
	return err;
}

LABS_EXPORT void labs_stream_connection_fini(LabsStreamConnection *stream_connection)
{
	free(stream_connection->remote_disconnect_reason);

	labs_gkcrypt_free(stream_connection->gkcrypt_remote);
	labs_gkcrypt_free(stream_connection->gkcrypt_local);

	free(stream_connection->ecdh_secret);
	if (stream_connection->congestion_control.thread.thread)
		labs_congestion_control_stop(&stream_connection->congestion_control);

	labs_packet_stats_fini(&stream_connection->packet_stats);

	labs_mutex_fini(&stream_connection->feedback_sender_mutex);

	labs_cond_fini(&stream_connection->state_cond);
	labs_mutex_fini(&stream_connection->state_mutex);
}

static bool state_finished_cond_check(void *user)
{
	LabsStreamConnection *stream_connection = user;
	return stream_connection->state_finished || stream_connection->should_stop || stream_connection->remote_disconnected;
}

LABS_EXPORT LabsErrorCode labs_stream_connection_run(LabsStreamConnection *stream_connection, labs_socket_t *socket)
{
	LabsSession *session = stream_connection->session;
	LabsErrorCode err;

	LabsTakionConnectInfo takion_info;
	takion_info.log = stream_connection->log;
	takion_info.disable_audio_video = stream_connection->session->connect_info.disable_audio_video;
	takion_info.close_socket = true;
	if(!socket)
	{
		takion_info.sa_len = session->connect_info.host_addrinfo_selected->ai_addrlen;
		takion_info.sa = malloc(takion_info.sa_len);
		if(!takion_info.sa)
			return LABS_ERR_MEMORY;
		memcpy(takion_info.sa, session->connect_info.host_addrinfo_selected->ai_addr, takion_info.sa_len);
		err = set_port(takion_info.sa, htons(STREAM_CONNECTION_PORT));
		assert(err == LABS_ERR_SUCCESS);
	}
	takion_info.ip_dontfrag = session->dontfrag;

	takion_info.enable_crypt = true;
	takion_info.enable_dualsense = session->connect_info.enable_dualsense;
	takion_info.protocol_version = labs_target_is_ps5(session->target) ? 12 : 9;

	takion_info.cb = stream_connection_takion_cb;
	takion_info.cb_user = stream_connection;

	err = labs_mutex_lock(&stream_connection->state_mutex);
	assert(err == LABS_ERR_SUCCESS);

#define CHECK_STOP(quit_label) do { \
	if(stream_connection->should_stop) \
	{ \
		goto quit_label; \
	} } while(0)

	stream_connection->audio_receiver = labs_audio_receiver_new(session, &stream_connection->packet_stats);
	if(!stream_connection->audio_receiver)
	{
		LABS_LOGE(session->log, "StreamConnection failed to initialize Audio Receiver");
		if(!socket)
			free(takion_info.sa);
		labs_mutex_unlock(&stream_connection->state_mutex);
		return LABS_ERR_UNKNOWN;
	}

	stream_connection->haptics_receiver = labs_audio_receiver_new(session, NULL);
	if(!stream_connection->haptics_receiver)
	{
		LABS_LOGE(session->log, "StreamConnection failed to initialize Haptics Receiver");
		err = LABS_ERR_UNKNOWN;
		labs_mutex_unlock(&stream_connection->state_mutex);
		goto err_audio_receiver;
	}

	stream_connection->video_receiver = labs_video_receiver_new(session, &stream_connection->packet_stats);
	if(!stream_connection->video_receiver)
	{
		LABS_LOGE(session->log, "StreamConnection failed to initialize Video Receiver");
		err = LABS_ERR_UNKNOWN;
		labs_mutex_unlock(&stream_connection->state_mutex);
		goto err_haptics_receiver;
	}

	stream_connection->state = STATE_TAKION_CONNECT;
	stream_connection->state_finished = false;
	stream_connection->state_failed = false;
	err = labs_takion_connect(&stream_connection->takion, &takion_info, socket);

	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(session->log, "StreamConnection connect failed");
		labs_mutex_unlock(&stream_connection->state_mutex);
		goto err_video_receiver;
	}

	err = labs_congestion_control_start(&stream_connection->congestion_control, &stream_connection->takion, &stream_connection->packet_stats, stream_connection->packet_loss_max);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(session->log, "StreamConnection failed to start Congestion Control");
		goto close_takion;
	}

	err = labs_cond_timedwait_pred(&stream_connection->state_cond, &stream_connection->state_mutex, EXPECT_TIMEOUT_MS, state_finished_cond_check, stream_connection);
	assert(err == LABS_ERR_SUCCESS || err == LABS_ERR_TIMEOUT);
	CHECK_STOP(close_takion);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(session->log, "StreamConnection Takion connect failed");
		goto err_congestion_control;
	}

	LABS_LOGI(session->log, "StreamConnection sending big");

	stream_connection->state = STATE_EXPECT_BANG;
	stream_connection->state_finished = false;
	stream_connection->state_failed = false;
	err = stream_connection_send_big(stream_connection);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(session->log, "StreamConnection failed to send big");
		goto disconnect;
	}

	err = labs_cond_timedwait_pred(&stream_connection->state_cond, &stream_connection->state_mutex, EXPECT_TIMEOUT_MS, state_finished_cond_check, stream_connection);
	assert(err == LABS_ERR_SUCCESS || err == LABS_ERR_TIMEOUT);
	CHECK_STOP(disconnect);

	if(!stream_connection->state_finished)
	{
		if(err == LABS_ERR_TIMEOUT)
			LABS_LOGE(session->log, "StreamConnection bang receive timeout");

		LABS_LOGE(session->log, "StreamConnection didn't receive bang or failed to handle it");
		err = LABS_ERR_UNKNOWN;
		goto disconnect;
	}
	LABS_LOGI(session->log, "StreamConnection successfully received bang");
	stream_connection->state = STATE_EXPECT_STREAMINFO;
	stream_connection->state_finished = false;
	stream_connection->state_failed = false;
	if(stream_connection->streaminfo_early_buf)
	{
		stream_connection_takion_data_expect_streaminfo(stream_connection, stream_connection->streaminfo_early_buf, stream_connection->streaminfo_early_buf_size);
		free(stream_connection->streaminfo_early_buf);
		stream_connection->streaminfo_early_buf = NULL;
	}
	if(!stream_connection->state_finished)
		err = labs_cond_timedwait_pred(&stream_connection->state_cond, &stream_connection->state_mutex, EXPECT_TIMEOUT_MS, state_finished_cond_check, stream_connection);
	assert(err == LABS_ERR_SUCCESS || err == LABS_ERR_TIMEOUT);
	CHECK_STOP(disconnect);

	if(!stream_connection->state_finished)
	{
		if(err == LABS_ERR_TIMEOUT)
			LABS_LOGE(session->log, "StreamConnection streaminfo receive timeout");

		LABS_LOGE(session->log, "StreamConnection didn't receive streaminfo");
		err = LABS_ERR_UNKNOWN;
		goto disconnect;
	}

	LABS_LOGI(session->log, "StreamConnection successfully received streaminfo");

	err = labs_mutex_lock(&stream_connection->feedback_sender_mutex);
	assert(err == LABS_ERR_SUCCESS);
	err = labs_feedback_sender_init(&stream_connection->feedback_sender, &stream_connection->takion);
	if(err != LABS_ERR_SUCCESS)
	{
		labs_mutex_unlock(&stream_connection->feedback_sender_mutex);
		LABS_LOGE(stream_connection->log, "StreamConnection failed to start Feedback Sender");
		goto disconnect;
	}
	stream_connection->feedback_sender_active = true;
	labs_feedback_sender_set_controller_state(&stream_connection->feedback_sender, &session->controller_state);
	labs_mutex_unlock(&stream_connection->feedback_sender_mutex);

	stream_connection->state = STATE_IDLE;
	stream_connection->state_finished = false;
	stream_connection->state_failed = false;

	LabsEvent event = { 0 };
	event.type = LABS_EVENT_CONNECTED;
	labs_mutex_unlock(&stream_connection->state_mutex);
	labs_session_send_event(session, &event);
	err = labs_mutex_lock(&stream_connection->state_mutex);
	assert(err == LABS_ERR_SUCCESS);

	while(true)
	{
		err = labs_cond_timedwait_pred(&stream_connection->state_cond, &stream_connection->state_mutex, HEARTBEAT_INTERVAL_MS, state_finished_cond_check, stream_connection);
		if(err != LABS_ERR_TIMEOUT)
			break;

		err = stream_connection_send_heartbeat(stream_connection);
		if(err != LABS_ERR_SUCCESS)
			LABS_LOGE(stream_connection->log, "StreamConnection failed to send heartbeat");
		else
			LABS_LOGV(stream_connection->log, "StreamConnection sent heartbeat");
	}

	err = labs_mutex_lock(&stream_connection->feedback_sender_mutex);
	assert(err == LABS_ERR_SUCCESS);
	stream_connection->feedback_sender_active = false;
	labs_feedback_sender_fini(&stream_connection->feedback_sender);
	labs_mutex_unlock(&stream_connection->feedback_sender_mutex);

	err = LABS_ERR_SUCCESS;

disconnect:
	LABS_LOGI(session->log, "StreamConnection is disconnecting");
	if(stream_connection->streaminfo_early_buf)
	{
		free(stream_connection->streaminfo_early_buf);
		stream_connection->streaminfo_early_buf = NULL;
	}
	stream_connection_send_disconnect(stream_connection);

	if(stream_connection->should_stop)
	{
		LABS_LOGI(stream_connection->log, "StreamConnection was requested to stop");
		err = LABS_ERR_CANCELED;
	}
	else if(stream_connection->remote_disconnected)
	{
		LABS_LOGI(stream_connection->log, "StreamConnection closing after Remote disconnected");
		err = LABS_ERR_DISCONNECTED;
	}

err_congestion_control:
	labs_congestion_control_stop(&stream_connection->congestion_control);

close_takion:
	labs_mutex_unlock(&stream_connection->state_mutex);

	labs_takion_close(&stream_connection->takion);
	LABS_LOGI(session->log, "StreamConnection closed takion");

err_video_receiver:
	labs_mutex_lock(&stream_connection->state_mutex);
	labs_video_receiver_free(stream_connection->video_receiver);
	stream_connection->video_receiver = NULL;
	labs_mutex_unlock(&stream_connection->state_mutex);

err_haptics_receiver:
	labs_mutex_lock(&stream_connection->state_mutex);
	labs_audio_receiver_free(stream_connection->haptics_receiver);
	stream_connection->haptics_receiver = NULL;
	labs_mutex_unlock(&stream_connection->state_mutex);

err_audio_receiver:
	labs_mutex_lock(&stream_connection->state_mutex);
	labs_audio_receiver_free(stream_connection->audio_receiver);
	stream_connection->audio_receiver = NULL;
	labs_mutex_unlock(&stream_connection->state_mutex);
	if(!socket)
		free(takion_info.sa);
	return err;
}

LABS_EXPORT LabsErrorCode labs_stream_connection_stop(LabsStreamConnection *stream_connection)
{
	LabsErrorCode err = labs_mutex_lock(&stream_connection->state_mutex);
	if(err != LABS_ERR_SUCCESS)
		return err;
	stream_connection->should_stop = true;
	LabsErrorCode unlock_err = labs_mutex_unlock(&stream_connection->state_mutex);
	err = labs_cond_signal(&stream_connection->state_cond);
	return err == LABS_ERR_SUCCESS ? unlock_err : err;
}

static void stream_connection_takion_cb(LabsTakionEvent *event, void *user)
{
	LabsStreamConnection *stream_connection = user;
	switch(event->type)
	{
		case LABS_TAKION_EVENT_TYPE_CONNECTED:
		case LABS_TAKION_EVENT_TYPE_DISCONNECT:
			labs_mutex_lock(&stream_connection->state_mutex);
			if(stream_connection->state == STATE_TAKION_CONNECT)
			{
				stream_connection->state_finished = event->type == LABS_TAKION_EVENT_TYPE_CONNECTED;
				stream_connection->state_failed = event->type == LABS_TAKION_EVENT_TYPE_DISCONNECT;
				labs_cond_signal(&stream_connection->state_cond);
			}
			labs_mutex_unlock(&stream_connection->state_mutex);
			break;
		case LABS_TAKION_EVENT_TYPE_DATA:
			stream_connection_takion_data(stream_connection, event->data.data_type, event->data.buf, event->data.buf_size);
			break;
		case LABS_TAKION_EVENT_TYPE_AV:
			stream_connection_takion_av(stream_connection, event->av);
			break;
		default:
			break;
	}
}

static void stream_connection_takion_data(LabsStreamConnection *stream_connection, LabsTakionMessageDataType data_type, uint8_t *buf, size_t buf_size)
{
	switch(data_type)
	{
		case LABS_TAKION_MESSAGE_DATA_TYPE_PROTOBUF:
			stream_connection_takion_data_protobuf(stream_connection, buf, buf_size);
			break;
		case LABS_TAKION_MESSAGE_DATA_TYPE_RUMBLE:
			stream_connection_takion_data_rumble(stream_connection, buf, buf_size);
			break;
		case LABS_TAKION_MESSAGE_DATA_TYPE_PAD_INFO:
			stream_connection_takion_data_pad_info(stream_connection, buf, buf_size);
			break;
		case LABS_TAKION_MESSAGE_DATA_TYPE_TRIGGER_EFFECTS:
			stream_connection_takion_data_trigger_effects(stream_connection, buf, buf_size);
			break;
		default:
			break;
	}
}

static void stream_connection_takion_data_protobuf(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	labs_mutex_lock(&stream_connection->state_mutex);
	switch(stream_connection->state)
	{
		case STATE_EXPECT_BANG:
			stream_connection_takion_data_expect_bang(stream_connection, buf, buf_size);
			break;
		case STATE_EXPECT_STREAMINFO:
			stream_connection_takion_data_expect_streaminfo(stream_connection, buf, buf_size);
			break;
		default: // STATE_IDLE
			stream_connection_takion_data_idle(stream_connection, buf, buf_size);
			break;
	}
	labs_mutex_unlock(&stream_connection->state_mutex);
}

static void stream_connection_takion_data_rumble(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	if(buf_size < 3)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection got rumble packet with size %#llx < 3",
				(unsigned long long)buf_size);
		return;
	}
	LabsEvent event = { 0 };
	event.type = LABS_EVENT_RUMBLE;
	event.rumble.unknown = buf[0];
	event.rumble.left = buf[1];
	event.rumble.right = buf[2];
	labs_session_send_event(stream_connection->session, &event);
}


static void stream_connection_takion_data_trigger_effects(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	if(buf_size < 25)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection got trigger effects packet with size %#llx < 25",
				(unsigned long long)buf_size);
		return;
	}
	LabsEvent event = { 0 };
	event.type = LABS_EVENT_TRIGGER_EFFECTS;
	event.trigger_effects.type_left = buf[1];
	event.trigger_effects.type_right = buf[2];
	memcpy(&event.trigger_effects.left, buf + 5, 10);
	memcpy(&event.trigger_effects.right, buf + 15, 10);
	labs_session_send_event(stream_connection->session, &event);
}

static char* DualSenseIntensity(LabsDualSenseEffectIntensity intensity)
{
	switch(intensity)
	{
		case Strong:
			return "Strong";
			break;
		case Weak:
			return "Weak";
			break;
		case Medium:
			return "Medium";
			break;
		case Off:
			return "Off";
			break;
		default:
			return "Invalid";
			break;
	}
}
static void stream_connection_takion_data_pad_info(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	bool led_changed = false;
	bool player_index_changed = false;
	bool motion_reset = false;
	bool haptic_intensity_changed = false;
	bool trigger_intensity_changed = false;

	LABS_LOGV(stream_connection->log, "Pad info packet: ");
	labs_log_hexdump(stream_connection->log, LABS_LOG_VERBOSE, buf, buf_size);

	switch(buf_size)
	{
		case 0x19:
		{
			// sequence number of feedback packet this is responding to
			uint16_t feedback_packet_seq_num = ntohs(*(labs_unaligned_uint16_t *)(buf));
			// int16_t unknown = ntohs(*(labs_unaligned_uint16_t *)(buf + 2));
			uint32_t timestamp = ntohs(*(labs_unaligned_uint32_t *)(buf + 4));
			if(stream_connection->haptic_intensity != buf[20])
			{
				stream_connection->haptic_intensity = buf[20];
				haptic_intensity_changed = true;
			}
			if(stream_connection->trigger_intensity != buf[21])
			{
				stream_connection->trigger_intensity = buf[21];
				trigger_intensity_changed = true;
			}
			if(buf[12])
			{
				motion_reset = true;
				LABS_LOGV(stream_connection->log, "StreamConnection received motion reset request in response to feedback packet with seqnum %"PRIu16"x , %"PRIu32" seconds after stream began", feedback_packet_seq_num, timestamp);
			}
			if(buf[8] != stream_connection->player_index)
			{
				player_index_changed = true;
				stream_connection->player_index = buf[8];
			}
			if(memcmp(buf + 9, stream_connection->led_state, 3) != 0)
			{
				led_changed = true;
				memcpy(stream_connection->led_state, buf + 9, 3);
			}
			break;
		}
		case 0x11:
		{
			if(stream_connection->haptic_intensity != buf[12])
			{
				stream_connection->haptic_intensity = buf[12];
				haptic_intensity_changed = true;
			}
			if(stream_connection->trigger_intensity != buf[13])
			{
				stream_connection->trigger_intensity = buf[13];
				trigger_intensity_changed = true;
			}
			if(buf[4])
			{
				motion_reset = true;
			}
			if(buf[0] != stream_connection->player_index)
			{
				player_index_changed = true;
				stream_connection->player_index = buf[0];
			}
			if(memcmp(buf + 1, stream_connection->led_state, 3) != 0)
			{
				led_changed = true;
				memcpy(stream_connection->led_state, buf + 1, 3);
			}
			break;
		}
		default:
		{
			LABS_LOGE(stream_connection->log, "StreamConnection got pad info with size %#llx not equal to 0x19 or 0x11",
					(unsigned long long)buf_size);
			return;
		}
	}
	if(motion_reset)
	{
		LABS_LOGI(stream_connection->log, "Setting motion control origin to current position");
		LabsEvent event = { 0 };
		event.type = LABS_EVENT_MOTION_RESET;
		labs_session_send_event(stream_connection->session, &event);
	}
	if(haptic_intensity_changed)
	{
		LABS_LOGI(stream_connection->log, "Set haptic intensity to: %s", DualSenseIntensity(stream_connection->haptic_intensity));
		LabsEvent event = { 0 };
		event.type = LABS_EVENT_HAPTIC_INTENSITY;
		event.intensity = stream_connection->haptic_intensity;
		labs_session_send_event(stream_connection->session, &event);
	}
	if(trigger_intensity_changed)
	{
		LABS_LOGI(stream_connection->log, "Set adaptive trigger intensity to: %s", DualSenseIntensity(stream_connection->trigger_intensity));
		LabsEvent event = { 0 };
		event.type = LABS_EVENT_TRIGGER_INTENSITY;
		event.intensity = stream_connection->trigger_intensity;
		labs_session_send_event(stream_connection->session, &event);
	}
	if(led_changed)
	{
		LABS_LOGV(stream_connection->log, "Set LED state to - red: %x, green: %x, blue: %x", stream_connection->led_state[0], stream_connection->led_state[1], stream_connection->led_state[2]);
		LabsEvent event = { 0 };
		event.type = LABS_EVENT_LED_COLOR;
		memcpy(event.led_state, stream_connection->led_state, 3);
		labs_session_send_event(stream_connection->session, &event);
	}
	if(player_index_changed)
	{
		LABS_LOGV(stream_connection->log, "Set player index to - %d", stream_connection->player_index);
		LabsEvent event = { 0 };
		event.type = LABS_EVENT_PLAYER_INDEX;
		event.player_index = stream_connection->player_index;
		labs_session_send_event(stream_connection->session, &event);
	}
}

static void stream_connection_takion_data_handle_disconnect(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	char reason[256];
	LabsPBDecodeBuf decode_buf;
	decode_buf.size = 0;
	decode_buf.max_size = sizeof(reason) - 1;
	decode_buf.buf = (uint8_t *)reason;
	msg.disconnect_payload.reason.arg = &decode_buf;
	msg.disconnect_payload.reason.funcs.decode = labs_pb_decode_buf;

	pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);
	bool r = pb_decode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!r)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to decode data protobuf");
		labs_log_hexdump(stream_connection->log, LABS_LOG_ERROR, buf, buf_size);
		return;
	}

	reason[decode_buf.size] = '\0';
	LABS_LOGI(stream_connection->log, "Remote disconnected from StreamConnection with reason \"%s\"", reason);

	stream_connection->remote_disconnected = true;
	free(stream_connection->remote_disconnect_reason);
	stream_connection->remote_disconnect_reason = strdup(reason);
	labs_cond_signal(&stream_connection->state_cond);
}

static void stream_connection_takion_data_idle(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);
	bool r = pb_decode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!r)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to decode data protobuf");
		labs_log_hexdump(stream_connection->log, LABS_LOG_ERROR, buf, buf_size);
		return;
	}

	LABS_LOGV(stream_connection->log, "StreamConnection received data with msg.type == %d", msg.type);
	labs_log_hexdump(stream_connection->log, LABS_LOG_VERBOSE, buf, buf_size);

	switch (msg.type)
	{
	case tkproto_TakionMessage_PayloadType_DISCONNECT:
		stream_connection_takion_data_handle_disconnect(stream_connection, buf, buf_size);
		break;
	case tkproto_TakionMessage_PayloadType_CONNECTIONQUALITY:
	{
		tkproto_ConnectionQualityPayload q = msg.connection_quality_payload;
		LABS_LOGV(
			stream_connection->log,
			"StreamConnection received connection quality: target_bitrate=%d, "
			"upstream_bitrate=%d, upstream_loss=%.4f, "
			"disable_upstream_audio=%d, rtt=%.4f, loss=%lld",
			 q.target_bitrate, q.upstream_bitrate,
			 q.upstream_loss,
			 q.disable_upstream_audio, q.rtt, q.loss);
		stream_connection->measured_bitrate = labs_stream_stats_bitrate(&stream_connection->video_receiver->frame_processor.stream_stats, stream_connection->session->connect_info.video_profile.max_fps) / 1000000.0;
		LABS_LOGV(stream_connection->log, "StreamConnection measured bitrate: %.4f MBit/s", stream_connection->measured_bitrate);
		labs_stream_stats_reset(&stream_connection->video_receiver->frame_processor.stream_stats);
		break;
	}
	case tkproto_TakionMessage_PayloadType_CORRUPTFRAME:
		LABS_LOGE(stream_connection->log, "StreamConnection received corrupt frame from %d to %d",
			msg.corrupt_payload.start, msg.corrupt_payload.end);
		break;
	case tkproto_TakionMessage_PayloadType_STREAMINFOACK:
		LABS_LOGV(stream_connection->log, "StreamConnection received streaminfo ack");
		break;
	default:
		break;
	}
}

static LabsErrorCode stream_connection_init_crypt(LabsStreamConnection *stream_connection)
{
	LabsSession *session = stream_connection->session;

	stream_connection->gkcrypt_local = labs_gkcrypt_new(stream_connection->log, LABS_GKCRYPT_KEY_BUF_BLOCKS_DEFAULT, 2, session->handshake_key, stream_connection->ecdh_secret);
	if(!stream_connection->gkcrypt_local)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to initialize local GKCrypt with index 2");
		return LABS_ERR_UNKNOWN;
	}
	stream_connection->gkcrypt_remote = labs_gkcrypt_new(stream_connection->log, LABS_GKCRYPT_KEY_BUF_BLOCKS_DEFAULT, 3, session->handshake_key, stream_connection->ecdh_secret);
	if(!stream_connection->gkcrypt_remote)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to initialize remote GKCrypt with index 3");
		free(stream_connection->gkcrypt_local);
		stream_connection->gkcrypt_local = NULL;
		return LABS_ERR_UNKNOWN;
	}

	labs_takion_set_crypt(&stream_connection->takion, stream_connection->gkcrypt_local, stream_connection->gkcrypt_remote);

	return LABS_ERR_SUCCESS;
}

static void stream_connection_takion_data_expect_bang(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	char ecdh_pub_key[128];
	LabsPBDecodeBuf ecdh_pub_key_buf = { sizeof(ecdh_pub_key), 0, (uint8_t *)ecdh_pub_key };
	char ecdh_sig[32];
	LabsPBDecodeBuf ecdh_sig_buf = { sizeof(ecdh_sig), 0, (uint8_t *)ecdh_sig };

	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	msg.bang_payload.ecdh_pub_key.arg = &ecdh_pub_key_buf;
	msg.bang_payload.ecdh_pub_key.funcs.decode = labs_pb_decode_buf;
	msg.bang_payload.ecdh_sig.arg = &ecdh_sig_buf;
	msg.bang_payload.ecdh_sig.funcs.decode = labs_pb_decode_buf;

	pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);
	bool r = pb_decode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!r)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to decode data protobuf");
		return;
	}

	if(msg.type != tkproto_TakionMessage_PayloadType_BANG || !msg.has_bang_payload)
	{
		if(msg.type == tkproto_TakionMessage_PayloadType_DISCONNECT)
		{
			stream_connection_takion_data_handle_disconnect(stream_connection, buf, buf_size);
			return;
		}
		if(msg.type == tkproto_TakionMessage_PayloadType_STREAMINFO)
		{
			if(!stream_connection->streaminfo_early_buf)
			{
				stream_connection->streaminfo_early_buf = malloc(buf_size);
				memcpy(stream_connection->streaminfo_early_buf, buf, buf_size);
				stream_connection->streaminfo_early_buf_size = buf_size;
				LABS_LOGI(stream_connection->log, "StreamConnection received streaminfo early, saving ...");
				return;
			}

		}
		LABS_LOGW(stream_connection->log, "StreamConnection expected bang payload but received something else: %d", msg.type);
		labs_log_hexdump(stream_connection->log, LABS_LOG_WARNING, buf, buf_size);
		return;
	}

	LABS_LOGI(stream_connection->log, "BANG received");

	if(!msg.bang_payload.version_accepted)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection bang remote didn't accept version");
		goto error;
	}

	if(!msg.bang_payload.encrypted_key_accepted)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection bang remote didn't accept encrypted key");
		goto error;
	}

	if(!ecdh_pub_key_buf.size)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection didn't get remote ECDH pub key from bang");
		goto error;
	}

	if(!ecdh_sig_buf.size)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection didn't get remote ECDH sig from bang");
		goto error;
	}

	assert(!stream_connection->ecdh_secret);
	stream_connection->ecdh_secret = malloc(LABS_ECDH_SECRET_SIZE);
	if(!stream_connection->ecdh_secret)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to alloc ECDH secret memory");
		goto error;
	}

	LabsErrorCode err = labs_ecdh_derive_secret(&stream_connection->session->ecdh,
			stream_connection->ecdh_secret,
			ecdh_pub_key_buf.buf, ecdh_pub_key_buf.size,
			stream_connection->session->handshake_key,
			ecdh_sig_buf.buf, ecdh_sig_buf.size);

	if(err != LABS_ERR_SUCCESS)
	{
		free(stream_connection->ecdh_secret);
		stream_connection->ecdh_secret = NULL;
		LABS_LOGE(stream_connection->log, "StreamConnection failed to derive secret from bang");
		goto error;
	}

	err = stream_connection_init_crypt(stream_connection);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to init crypt after receiving bang");
		goto error;
	}

	// stream_connection->state_mutex is expected to be locked by the caller of this function
	stream_connection->state_finished = true;
	labs_cond_signal(&stream_connection->state_cond);
	return;
error:
	stream_connection->state_failed = true;
	labs_cond_signal(&stream_connection->state_cond);
}

typedef struct decode_resolutions_context_t
{
	LabsStreamConnection *stream_connection;
	LabsVideoProfile video_profiles[LABS_VIDEO_PROFILES_MAX];
	size_t video_profiles_count;
} DecodeResolutionsContext;

static bool pb_decode_resolution(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	DecodeResolutionsContext *ctx = *arg;

	tkproto_ResolutionPayload resolution = { 0 };
	LabsPBDecodeBufAlloc header_buf = { 0 };
	resolution.video_header.arg = &header_buf;
	resolution.video_header.funcs.decode = labs_pb_decode_buf_alloc;
	if(!pb_decode(stream, tkproto_ResolutionPayload_fields, &resolution))
		return false;

	if(!header_buf.buf)
	{
		LABS_LOGE(ctx->stream_connection->session->log, "Failed to decode video header");
		return true;
	}

	uint8_t *header_buf_padded = realloc(header_buf.buf, header_buf.size + LABS_VIDEO_BUFFER_PADDING_SIZE);
	if(!header_buf_padded)
	{
		free(header_buf.buf);
		LABS_LOGE(ctx->stream_connection->session->log, "Failed to realloc video header with padding");
		return true;
	}
	memset(header_buf_padded + header_buf.size, 0, LABS_VIDEO_BUFFER_PADDING_SIZE);

	if(ctx->video_profiles_count >= LABS_VIDEO_PROFILES_MAX)
	{
		LABS_LOGE(ctx->stream_connection->session->log, "Received more resolutions than the maximum");
		return true;
	}

	LabsVideoProfile *profile = &ctx->video_profiles[ctx->video_profiles_count++];
	profile->width = resolution.width;
	profile->height = resolution.height;
	profile->header_sz = header_buf.size;
	profile->header = header_buf_padded;
	return true;
}

static void stream_connection_takion_data_expect_streaminfo(LabsStreamConnection *stream_connection, uint8_t *buf, size_t buf_size)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	uint8_t audio_header[LABS_AUDIO_HEADER_SIZE];
	LabsPBDecodeBuf audio_header_buf = { sizeof(audio_header), 0, (uint8_t *)audio_header };
	msg.stream_info_payload.audio_header.arg = &audio_header_buf;
	msg.stream_info_payload.audio_header.funcs.decode = labs_pb_decode_buf;

	DecodeResolutionsContext decode_resolutions_context;
	decode_resolutions_context.stream_connection = stream_connection;
	memset(decode_resolutions_context.video_profiles, 0, sizeof(decode_resolutions_context.video_profiles));
	decode_resolutions_context.video_profiles_count = 0;
	msg.stream_info_payload.resolution.arg = &decode_resolutions_context;
	msg.stream_info_payload.resolution.funcs.decode = pb_decode_resolution;

	pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);
	bool r = pb_decode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!r)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to decode data protobuf");
		return;
	}

	if(msg.type != tkproto_TakionMessage_PayloadType_STREAMINFO || !msg.has_stream_info_payload)
	{
		if(msg.type == tkproto_TakionMessage_PayloadType_DISCONNECT)
		{
			stream_connection_takion_data_handle_disconnect(stream_connection, buf, buf_size);
			return;
		}

		LABS_LOGW(stream_connection->log, "StreamConnection expected streaminfo payload but received something else");
		labs_log_hexdump(stream_connection->log, LABS_LOG_WARNING, buf, buf_size);
		return;
	}

	LABS_LOGI(stream_connection->log, "StreamConnection received audio header:");
	labs_log_hexdump(stream_connection->log, LABS_LOG_DEBUG, audio_header, audio_header_buf.size);

	if(audio_header_buf.size != LABS_AUDIO_HEADER_SIZE)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection received invalid audio header in streaminfo");
		goto error;
	}

	LabsAudioHeader audio_header_s;
	labs_audio_header_load(&audio_header_s, audio_header);
	labs_audio_receiver_stream_info(stream_connection->audio_receiver, &audio_header_s);

	labs_video_receiver_stream_info(stream_connection->video_receiver,
			decode_resolutions_context.video_profiles,
			decode_resolutions_context.video_profiles_count);

	// TODO: do some checks?

	stream_connection_send_streaminfo_ack(stream_connection);
	
	LabsErrorCode err = stream_connection_send_controller_connection(stream_connection);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to send controller connection");
		goto error;
	}

	err = stream_connection_enable_microphone(stream_connection);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to enable microphone input");
		goto error;
	}

	// stream_connection->state_mutex is expected to be locked by the caller of this function
	stream_connection->state_finished = true;
	labs_cond_signal(&stream_connection->state_cond);
	return;
error:
	stream_connection->state_failed = true;
	labs_cond_signal(&stream_connection->state_cond);
}

static bool labs_pb_encode_zero_encrypted_key(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	if(!pb_encode_tag_for_field(stream, field))
		return false;
	uint8_t data[] = { 0, 0, 0, 0 };
	return pb_encode_string(stream, data, sizeof(data));
}

#define LAUNCH_SPEC_JSON_BUF_SIZE 1024

static LabsErrorCode stream_connection_send_big(LabsStreamConnection *stream_connection)
{
	LabsSession *session = stream_connection->session;

	LabsLaunchSpec launch_spec;
	launch_spec.target = session->target;
	launch_spec.mtu = session->mtu_in;
	launch_spec.rtt = session->rtt_us / 1000;
	launch_spec.handshake_key = session->handshake_key;

	launch_spec.width = session->connect_info.video_profile.width;
	launch_spec.height = session->connect_info.video_profile.height;
	launch_spec.max_fps = session->connect_info.video_profile.max_fps;
	launch_spec.codec = session->connect_info.video_profile.codec;
	launch_spec.bw_kbps_sent = session->connect_info.video_profile.bitrate;

	union
	{
		char json[LAUNCH_SPEC_JSON_BUF_SIZE];
		char b64[LAUNCH_SPEC_JSON_BUF_SIZE * 2];
	} launch_spec_buf;
	int launch_spec_json_size = labs_launchspec_format(launch_spec_buf.json, sizeof(launch_spec_buf.json), &launch_spec);
	if(launch_spec_json_size < 0)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to format LaunchSpec json");
		return LABS_ERR_UNKNOWN;
	}
	launch_spec_json_size += 1; // we also want the trailing 0

	LABS_LOGV(stream_connection->log, "LaunchSpec: %s", launch_spec_buf.json);

	uint8_t launch_spec_json_enc[LAUNCH_SPEC_JSON_BUF_SIZE];
	memset(launch_spec_json_enc, 0, (size_t)launch_spec_json_size);
	LabsErrorCode err = labs_rpcrypt_encrypt(&session->rpcrypt, 0, launch_spec_json_enc, launch_spec_json_enc,
			(size_t)launch_spec_json_size);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to encrypt LaunchSpec");
		return err;
	}

	xor_bytes(launch_spec_json_enc, (uint8_t *)launch_spec_buf.json, (size_t)launch_spec_json_size);
	err = labs_base64_encode(launch_spec_json_enc, (size_t)launch_spec_json_size, launch_spec_buf.b64, sizeof(launch_spec_buf.b64));
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to encode LaunchSpec as base64");
		return err;
	}

	uint8_t ecdh_pub_key[128];
	LabsPBBuf ecdh_pub_key_buf = { sizeof(ecdh_pub_key), ecdh_pub_key };
	uint8_t ecdh_sig[32];
	LabsPBBuf ecdh_sig_buf = { sizeof(ecdh_sig), ecdh_sig };
	err = labs_ecdh_get_local_pub_key(&session->ecdh,
			ecdh_pub_key, &ecdh_pub_key_buf.size,
			session->handshake_key,
			ecdh_sig, &ecdh_sig_buf.size);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection failed to get ECDH key and sig");
		return err;
	}

	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	msg.type = tkproto_TakionMessage_PayloadType_BIG;
	msg.has_big_payload = true;
	msg.big_payload.client_version = stream_connection->takion.version;
	msg.big_payload.session_key.arg = session->session_id;
	msg.big_payload.session_key.funcs.encode = labs_pb_encode_string;
	msg.big_payload.launch_spec.arg = launch_spec_buf.b64;
	msg.big_payload.launch_spec.funcs.encode = labs_pb_encode_string;
	msg.big_payload.encrypted_key.funcs.encode = labs_pb_encode_zero_encrypted_key;
	msg.big_payload.ecdh_pub_key.arg = &ecdh_pub_key_buf;
	msg.big_payload.ecdh_pub_key.funcs.encode = labs_pb_encode_buf;
	msg.big_payload.ecdh_sig.arg = &ecdh_sig_buf;
	msg.big_payload.ecdh_sig.funcs.encode = labs_pb_encode_buf;

	uint8_t buf[2048];
	size_t buf_size;

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection big protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	int32_t total_size = stream.bytes_written;
	uint32_t mtu = (session->mtu_in < session->mtu_out) ? session->mtu_in : session->mtu_out;
	// Take into account overhead of network
	mtu -= 50;
	uint32_t buf_pos = 0;
	bool first = true;
	while((mtu < total_size + 26) || (mtu < total_size + 25 && !first))
	{
		if(first)
		{
			buf_size = mtu - 26;
			err = labs_takion_send_message_data(&stream_connection->takion, 0, 1, buf + buf_pos, buf_size, NULL);
			first = false;
		}
		else
		{
			buf_size = mtu - 25;
			err = labs_takion_send_message_data_cont(&stream_connection->takion, 0, 1, buf + buf_pos, buf_size, NULL);
		}
		buf_pos += buf_size;
		total_size -= buf_size;
	}
	if(total_size > 0)
	{
		if(first)
		  err = labs_takion_send_message_data(&stream_connection->takion, 1, 1, buf + buf_pos, total_size, NULL);
		else
		  err = labs_takion_send_message_data_cont(&stream_connection->takion, 1, 1, buf + buf_pos, total_size, NULL);
	}
	return err;
}

static LabsErrorCode stream_connection_send_controller_connection(LabsStreamConnection *stream_connection)
{
	LabsSession *session = stream_connection->session;
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	msg.type = tkproto_TakionMessage_PayloadType_CONTROLLERCONNECTION;
	msg.has_controller_connection_payload = true;
	msg.controller_connection_payload.has_connected = true;
	msg.controller_connection_payload.connected = true;
	msg.controller_connection_payload.has_controller_id = false;
	msg.controller_connection_payload.has_controller_type = true;
	msg.controller_connection_payload.controller_type = session->connect_info.enable_dualsense
		? tkproto_ControllerConnectionPayload_ControllerType_DUALSENSE
		: tkproto_ControllerConnectionPayload_ControllerType_DUALSHOCK4;

	uint8_t buf[2048];
	size_t buf_size;

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection controller connection protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	buf_size = stream.bytes_written;
	return labs_takion_send_message_data(&stream_connection->takion, 1, 1, buf, buf_size, NULL);
}

static LabsErrorCode stream_connection_enable_microphone(LabsStreamConnection *stream_connection)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	LabsAudioHeader audio_header_input;
	labs_audio_header_set(&audio_header_input, 16, 1, 48000, 480);
	uint8_t audio_header[LABS_AUDIO_HEADER_SIZE];
	labs_audio_header_save(&audio_header_input, audio_header);
	LabsPBBuf audio_header_buf = { sizeof(audio_header), (uint8_t *)audio_header };

	msg.type = tkproto_TakionMessage_PayloadType_STREAMINFO;
	msg.has_stream_info_payload = true;
	msg.stream_info_payload.audio_header.arg = &audio_header_buf;
	msg.stream_info_payload.audio_header.funcs.encode = labs_pb_encode_buf;
	msg.stream_info_payload.has_start_timeout = false;
	msg.stream_info_payload.has_afk_timeout = false;
	msg.stream_info_payload.has_afk_timeout_disconnect = false;
	msg.stream_info_payload.has_congestion_control_interval = false;

	uint8_t buf[2048];
	size_t buf_size;

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection controller connection protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	buf_size = stream.bytes_written;
	return labs_takion_send_message_data(&stream_connection->takion, 1, 1, buf, buf_size, NULL);
}

static LabsErrorCode stream_connection_send_streaminfo_ack(LabsStreamConnection *stream_connection)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));
	msg.type = tkproto_TakionMessage_PayloadType_STREAMINFOACK;

	uint8_t buf[3];
	size_t buf_size;

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection streaminfo ack protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	buf_size = stream.bytes_written;
	return labs_takion_send_message_data(&stream_connection->takion, 1, 9, buf, buf_size, NULL);
}

static LabsErrorCode stream_connection_send_disconnect(LabsStreamConnection *stream_connection)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	msg.type = tkproto_TakionMessage_PayloadType_DISCONNECT;
	msg.has_disconnect_payload = true;
	msg.disconnect_payload.reason.arg = "Client Disconnecting";
	msg.disconnect_payload.reason.funcs.encode = labs_pb_encode_string;

	uint8_t buf[26];
	size_t buf_size;

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection disconnect protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	LABS_LOGI(stream_connection->log, "StreamConnection sending Disconnect");

	buf_size = stream.bytes_written;
	LabsErrorCode err = labs_takion_send_message_data(&stream_connection->takion, 1, 1, buf, buf_size, NULL);

	return err;
}

static void stream_connection_takion_av(LabsStreamConnection *stream_connection, LabsTakionAVPacket *packet)
{
	labs_gkcrypt_decrypt(stream_connection->gkcrypt_remote, packet->key_pos + LABS_GKCRYPT_BLOCK_SIZE, packet->data, packet->data_size);

	if(packet->is_video)
		labs_video_receiver_av_packet(stream_connection->video_receiver, packet);
	else if(packet->is_haptics)
	    labs_audio_receiver_av_packet(stream_connection->haptics_receiver, packet);
	else
		labs_audio_receiver_av_packet(stream_connection->audio_receiver, packet);
}

static LabsErrorCode stream_connection_send_heartbeat(LabsStreamConnection *stream_connection)
{
	tkproto_TakionMessage msg = { 0 };
	msg.type = tkproto_TakionMessage_PayloadType_HEARTBEAT;

	uint8_t buf[8];

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection heartbeat protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	return labs_takion_send_message_data(&stream_connection->takion, 1, 1, buf, stream.bytes_written, NULL);
}

LABS_EXPORT LabsErrorCode stream_connection_send_corrupt_frame(LabsStreamConnection *stream_connection, LabsSeqNum16 start, LabsSeqNum16 end)
{
	tkproto_TakionMessage msg = { 0 };
	msg.type = tkproto_TakionMessage_PayloadType_CORRUPTFRAME;
	msg.has_corrupt_payload = true;
	msg.corrupt_payload.start = start;
	msg.corrupt_payload.end = end;

	uint8_t buf[0x10];

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection heartbeat protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	LABS_LOGW(stream_connection->log, "StreamConnection reporting corrupt frame(s) from %u to %u", (unsigned int)start, (unsigned int)end);
	return labs_takion_send_message_data(&stream_connection->takion, 1, 2, buf, stream.bytes_written, NULL);
}

LABS_EXPORT LabsErrorCode stream_connection_send_idr_request(LabsStreamConnection *stream_connection)
{
	tkproto_TakionMessage msg = { 0 };
	msg.type = tkproto_TakionMessage_PayloadType_IDRREQUEST;

	uint8_t buf[0x10];

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(stream_connection->log, "StreamConnection IDR request protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	LABS_LOGI(stream_connection->log, "StreamConnection requesting IDR frame");
	return labs_takion_send_message_data(&stream_connection->takion, 1, 2, buf, stream.bytes_written, NULL);
}
