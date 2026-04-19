// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/audioreceiver.h>
#include <labs/senkusha.h>
#include <labs/session.h>
#include <labs/http.h>
#include <labs/base64.h>
#include <labs/random.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>

#ifdef _WIN32
#include <winsock2.h>
#define strcasecmp _stricmp
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#include "utils.h"


#define SESSION_PORT					9295

#define SESSION_EXPECT_TIMEOUT_MS		5000

#define SESSION_EXPECT_CTRL_START_MS    10000

static void *session_thread_func(void *arg);
static void regist_cb(LabsRegistEvent *event, void *user);
static LabsErrorCode session_thread_request_session(LabsSession *session, LabsTarget *target_out);

const char *labs_rp_application_reason_string(uint32_t reason)
{
	switch(reason)
	{
		case LABS_RP_APPLICATION_REASON_REGIST_FAILED:
			return "Regist failed, probably invalid PIN";
		case LABS_RP_APPLICATION_REASON_INVALID_PSN_ID:
			return "Invalid PSN ID";
		case LABS_RP_APPLICATION_REASON_IN_USE:
			return "Remote is already in use";
		case LABS_RP_APPLICATION_REASON_CRASH:
			return "Remote Play on Console crashed";
		case LABS_RP_APPLICATION_REASON_RP_VERSION:
			return "RP-Version mismatch";
		default:
			return "unknown";
	}
}

const char *labs_rp_version_string(LabsTarget version)
{
	switch(version)
	{
		case LABS_TARGET_PS4_8:
			return "8.0";
		case LABS_TARGET_PS4_9:
			return "9.0";
		case LABS_TARGET_PS4_10:
			return "10.0";
		case LABS_TARGET_PS5_1:
			return "1.0";
		default:
			return NULL;
	}
}

LABS_EXPORT LabsTarget labs_rp_version_parse(const char *rp_version_str, bool is_ps5)
{
	if(is_ps5)
	{
		if(!strcmp(rp_version_str, "1.0"))
			return LABS_TARGET_PS5_1;
		return LABS_TARGET_PS5_UNKNOWN;
	}
	if(!strcmp(rp_version_str, "8.0"))
		return LABS_TARGET_PS4_8;
	if(!strcmp(rp_version_str, "9.0"))
		return LABS_TARGET_PS4_9;
	if(!strcmp(rp_version_str, "10.0"))
		return LABS_TARGET_PS4_10;
	return LABS_TARGET_PS4_UNKNOWN;
}

LABS_EXPORT void labs_connect_video_profile_preset(LabsConnectVideoProfile *profile, LabsVideoResolutionPreset resolution, LabsVideoFPSPreset fps)
{
	profile->codec = LABS_CODEC_H264;
	switch(resolution)
	{
		case LABS_VIDEO_RESOLUTION_PRESET_360p:
			profile->width = 640;
			profile->height = 360;
			profile->bitrate = 2000;
			break;
		case LABS_VIDEO_RESOLUTION_PRESET_540p:
			profile->width = 960;
			profile->height = 540;
			profile->bitrate = 6000;
			break;
		case LABS_VIDEO_RESOLUTION_PRESET_720p:
			profile->width = 1280;
			profile->height = 720;
			profile->bitrate = 10000;
			break;
		case LABS_VIDEO_RESOLUTION_PRESET_1080p:
			profile->width = 1920;
			profile->height = 1080;
			profile->bitrate = 15000;
			break;
		default:
			profile->width = 0;
			profile->height = 0;
			profile->bitrate = 0;
			break;
	}

	switch(fps)
	{
		case LABS_VIDEO_FPS_PRESET_30:
			profile->max_fps = 30;
			break;
		case LABS_VIDEO_FPS_PRESET_60:
			profile->max_fps = 60;
			break;
		default:
			profile->max_fps = 0;
			break;
	}
}

LABS_EXPORT const char *labs_quit_reason_string(LabsQuitReason reason)
{
	switch(reason)
	{
		case LABS_QUIT_REASON_STOPPED:
			return "Stopped";
		case LABS_QUIT_REASON_SESSION_REQUEST_UNKNOWN:
			return "Unknown Session Request Error";
		case LABS_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED:
			return "Connection Refused in Session Request";
		case LABS_QUIT_REASON_SESSION_REQUEST_RP_IN_USE:
			return "Remote Play on Console is already in use";
		case LABS_QUIT_REASON_SESSION_REQUEST_RP_CRASH:
			return "Remote Play on Console has crashed";
		case LABS_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH:
			return "RP-Version mismatch";
		case LABS_QUIT_REASON_CTRL_UNKNOWN:
			return "Unknown Ctrl Error";
		case LABS_QUIT_REASON_CTRL_CONNECTION_REFUSED:
			return "Connection Refused in Ctrl";
		case LABS_QUIT_REASON_CTRL_CONNECT_FAILED:
			return "Ctrl failed to connect";
		case LABS_QUIT_REASON_STREAM_CONNECTION_UNKNOWN:
			return "Unknown Error in Stream Connection";
		case LABS_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED:
			return "Remote has disconnected from Stream Connection";
		case LABS_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN:
			return "Remote has disconnected from Stream Connection the because Server shut down";
		case LABS_QUIT_REASON_PSN_REGIST_FAILED:
			return "The Console Registration using PSN has failed";
		case LABS_QUIT_REASON_NONE:
		default:
			return "Unknown";
	}
}

LABS_EXPORT LabsErrorCode labs_session_init(LabsSession *session, LabsConnectInfo *connect_info,
	LabsLog *log)
{
	memset(session, 0, sizeof(LabsSession));

	session->log = log;
	session->quit_reason = LABS_QUIT_REASON_NONE;
	session->target = connect_info->ps5 ? LABS_TARGET_PS5_1 : LABS_TARGET_PS4_10;
	session->auto_regist = connect_info->auto_regist;
	session->holepunch_session = connect_info->holepunch_session;
	session->rudp = NULL;
	session->dontfrag = true;

	LabsErrorCode err = labs_cond_init(&session->state_cond);
	if(err != LABS_ERR_SUCCESS)
		goto error;
		
	err = labs_mutex_init(&session->state_mutex, false);
	if(err != LABS_ERR_SUCCESS)
		goto error_state_cond;

	err = labs_stop_pipe_init(&session->stop_pipe);
	if(err != LABS_ERR_SUCCESS)
		goto error_state_mutex;

	labs_mutex_lock(&session->state_mutex);
	session->should_stop = false;
	session->ctrl_session_id_received = false;
	session->ctrl_login_pin_requested = false;
	session->login_pin_entered = false;
	session->psn_regist_succeeded = false;
	session->stream_connection_switch_received = false;
	session->login_pin = NULL;
	session->login_pin_size = 0;
	labs_mutex_unlock(&session->state_mutex);

	err = labs_ctrl_init(&session->ctrl, session);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(session->log, "Ctrl init failed");
		goto error_stop_pipe;
	}

	err = labs_stream_connection_init(&session->stream_connection, session, connect_info->packet_loss_max);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(session->log, "StreamConnection init failed");
		goto error_ctrl;
	}

	if(session->holepunch_session)
	{
		memcpy(session->connect_info.psn_account_id, connect_info->psn_account_id, sizeof(connect_info->psn_account_id));
	}
	else
	{
		// make hostname use ipv4 for now
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_socktype = SOCK_DGRAM;
		char *ipv6 = strchr(connect_info->host, ':');
		if(ipv6)
			hints.ai_family = AF_INET6;
		else
			hints.ai_family = AF_INET;
		int r = getaddrinfo(connect_info->host, NULL, &hints, &session->connect_info.host_addrinfos);
		if(r != 0)
		{
			labs_session_fini(session);
			return LABS_ERR_PARSE_ADDR;
		}
		memcpy(session->connect_info.regist_key, connect_info->regist_key, sizeof(session->connect_info.regist_key));
		memcpy(session->connect_info.morning, connect_info->morning, sizeof(session->connect_info.morning));
	}

	labs_controller_state_set_idle(&session->controller_state);

	session->connect_info.ps5 = connect_info->ps5;
	const uint8_t did_prefix[] = { 0x00, 0x18, 0x00, 0x00, 0x00, 0x07, 0x00, 0x40, 0x00, 0x80 };
	const uint8_t did_suffix[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	memcpy(session->connect_info.did, did_prefix, sizeof(did_prefix));
	labs_random_bytes_crypt(session->connect_info.did + sizeof(did_prefix), sizeof(session->connect_info.did) - sizeof(did_prefix) - sizeof(did_suffix));
	memcpy(session->connect_info.did + sizeof(session->connect_info.did) - sizeof(did_suffix), did_suffix, sizeof(did_suffix));

	if(connect_info->audio_video_disabled & LABS_VIDEO_DISABLED)
		labs_connect_video_profile_preset(&session->connect_info.video_profile, LABS_VIDEO_RESOLUTION_PRESET_360p, 0);
	else
		session->connect_info.video_profile = connect_info->video_profile;
	session->connect_info.disable_audio_video = connect_info->audio_video_disabled;
	session->connect_info.video_profile_auto_downgrade = connect_info->video_profile_auto_downgrade;
	session->connect_info.enable_keyboard = connect_info->enable_keyboard;
	session->connect_info.enable_dualsense = connect_info->enable_dualsense;
	session->connect_info.enable_idr_on_fec_failure = connect_info->enable_idr_on_fec_failure;

	return LABS_ERR_SUCCESS;

error_ctrl:
	labs_ctrl_fini(&session->ctrl);
error_stop_pipe:
	labs_stop_pipe_fini(&session->stop_pipe);
error_state_mutex:
	labs_mutex_fini(&session->state_mutex);
error_state_cond:
	labs_cond_fini(&session->state_cond);
error:
	if(session->holepunch_session)
		labs_holepunch_session_fini(session->holepunch_session);
	return err;
}

LABS_EXPORT void labs_session_fini(LabsSession *session)
{
	if(!session)
		return;
    LabsErrorCode err = labs_mutex_lock(&session->state_mutex);
	assert(err == LABS_ERR_SUCCESS);
	free(session->login_pin);
	free(session->quit_reason_str);
	labs_mutex_unlock(&session->state_mutex);
	labs_stream_connection_fini(&session->stream_connection);
	labs_ctrl_fini(&session->ctrl);
	if(session->rudp)
		labs_rudp_fini(session->rudp);
	if(session->holepunch_session)
		labs_holepunch_session_fini(session->holepunch_session);
	labs_stop_pipe_fini(&session->stop_pipe);
	labs_cond_fini(&session->state_cond);
	labs_mutex_fini(&session->state_mutex);
	freeaddrinfo(session->connect_info.host_addrinfos);
}

LABS_EXPORT LabsErrorCode labs_session_start(LabsSession *session)
{
	LabsErrorCode err = labs_thread_create(&session->session_thread, session_thread_func, session);
	if(err != LABS_ERR_SUCCESS)
		return err;
	labs_thread_set_name(&session->session_thread, "Labs Session");
	return err;
}

LABS_EXPORT LabsErrorCode labs_session_stop(LabsSession *session)
{
	LabsErrorCode err = labs_mutex_lock(&session->state_mutex);
	assert(err == LABS_ERR_SUCCESS);

	session->should_stop = true;
	labs_stop_pipe_stop(&session->stop_pipe);
	labs_cond_signal(&session->state_cond);

	labs_stream_connection_stop(&session->stream_connection);

	labs_mutex_unlock(&session->state_mutex);
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_session_join(LabsSession *session)
{
	return labs_thread_join(&session->session_thread, NULL);
}

LABS_EXPORT LabsErrorCode labs_session_request_idr(LabsSession *session)
{
	return stream_connection_send_idr_request(&session->stream_connection);
}

LABS_EXPORT LabsErrorCode labs_session_set_controller_state(LabsSession *session, LabsControllerState *state)
{
	LabsErrorCode err = labs_mutex_lock(&session->stream_connection.feedback_sender_mutex);
	if(err != LABS_ERR_SUCCESS)
		return err;
	session->controller_state = *state;
	if(session->stream_connection.feedback_sender_active)
		labs_feedback_sender_set_controller_state(&session->stream_connection.feedback_sender, &session->controller_state);
	labs_mutex_unlock(&session->stream_connection.feedback_sender_mutex);
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_session_set_login_pin(LabsSession *session, const uint8_t *pin, size_t pin_size)
{
	uint8_t *buf = malloc(pin_size);
	if(!buf){
		return LABS_ERR_MEMORY;
	}
	memcpy(buf, pin, pin_size);
	LabsErrorCode err = labs_mutex_lock(&session->state_mutex);
	assert(err == LABS_ERR_SUCCESS);
	if(session->login_pin_entered)
		free(session->login_pin);
	session->login_pin_entered = true;
	session->login_pin = buf;
	session->login_pin_size = pin_size;
	labs_mutex_unlock(&session->state_mutex);
	labs_cond_signal(&session->state_cond);
	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_session_set_stream_connection_switch_received(LabsSession *session)
{
	LabsErrorCode err = labs_mutex_lock(&session->state_mutex);
	assert(err == LABS_ERR_SUCCESS);
	session->stream_connection_switch_received = true;
	labs_mutex_unlock(&session->state_mutex);
	labs_cond_signal(&session->state_cond);
	return LABS_ERR_SUCCESS;
}

void labs_session_send_event(LabsSession *session, LabsEvent *event)
{
	if(!session->event_cb)
		return;
	session->event_cb(event, session->event_cb_user);
}


static bool session_check_state_pred(void *user)
{
	LabsSession *session = user;
	return session->should_stop
		|| session->ctrl_failed;
}

static bool session_check_state_pred_ctrl_start(void *user)
{
	LabsSession *session = user;
	return session->should_stop
		   || session->ctrl_failed
		   || session->ctrl_session_id_received
		   || session->ctrl_login_pin_requested;
}

static bool session_check_state_pred_pin(void *user)
{
	LabsSession *session = user;
	return session->should_stop
		   || session->ctrl_failed
		   || session->login_pin_entered;
}

static bool session_check_state_pred_stream_connection_switch(void *user)
{
	LabsSession *session = user;
	return session->should_stop
		|| session->ctrl_failed
		|| session->stream_connection_switch_received;
}

static bool session_check_state_pred_regist(void *user)
{
	LabsSession *session = user;
	return session->should_stop
		|| session->ctrl_failed
		|| session->psn_regist_succeeded;
}

#define ENABLE_SENKUSHA

static void *session_thread_func(void *arg)
{
	LabsSession *session = (LabsSession *)arg;
	labs_thread_set_affinity(LABS_THREAD_NAME_SESSION);

	labs_mutex_lock(&session->state_mutex);

#define QUIT(quit_label) do { \
	labs_mutex_unlock(&session->state_mutex); \
	goto quit_label; } while(0)

#define CHECK_STOP(quit_label) do { \
	if(session->should_stop) \
	{ \
		session->quit_reason = LABS_QUIT_REASON_STOPPED; \
		QUIT(quit_label); \
	} } while(0)

	CHECK_STOP(quit);

	if(session->holepunch_session)
	{
		labs_socket_t *rudp_sock = labs_get_holepunch_sock(session->holepunch_session, LABS_HOLEPUNCH_PORT_TYPE_CTRL);
		session->rudp = labs_rudp_init(rudp_sock, session->log);
		if(!session->rudp)
		{
			LABS_LOGE(session->log, "Initializing rudp failed");
			CHECK_STOP(quit);
		}
	}
	// PSN Connection
	if(session->rudp)
	{
		LabsRegist regist;
		LabsRegistInfo info;
		LabsHolepunchRegistInfo hinfo = labs_get_regist_info(session->holepunch_session);
		info.holepunch_info = &hinfo;
		info.host = NULL;
		info.broadcast = false;
		info.psn_online_id = NULL;
		info.pin = 0;
		info.console_pin = 0;
		memcpy(info.psn_account_id, session->connect_info.psn_account_id, LABS_PSN_ACCOUNT_ID_SIZE);
		info.rudp = session->rudp;
		info.target = session->connect_info.ps5 ? LABS_TARGET_PS5_1 : LABS_TARGET_PS4_10;
		labs_regist_start(&regist, session->log, &info, regist_cb, session);
		labs_cond_timedwait_pred(&session->state_cond, &session->state_mutex, 10000, session_check_state_pred_regist, session);
		labs_regist_stop(&regist);
		labs_regist_fini(&regist);
		CHECK_STOP(quit);
	}
	if(session->auto_regist)
	{
		LABS_LOGI(session->log, "Console auto registered successfully");
		session->quit_reason = LABS_QUIT_REASON_STOPPED;
		QUIT(quit);
	}
	LABS_LOGI(session->log, "Starting session request for %s", session->connect_info.ps5 ? "PS5" : "PS4");

	LabsTarget server_target = LABS_TARGET_PS4_UNKNOWN;
	LabsErrorCode err = session_thread_request_session(session, &server_target);

	if(err == LABS_ERR_VERSION_MISMATCH && !labs_target_is_unknown(server_target))
	{
		LABS_LOGI(session->log, "Attempting to re-request session with Server's RP-Version");
		session->target = server_target;
		err = session_thread_request_session(session, &server_target);
	}
	else if(err != LABS_ERR_SUCCESS)
		QUIT(quit);

	if(err == LABS_ERR_VERSION_MISMATCH && !labs_target_is_unknown(server_target))
	{
		LABS_LOGI(session->log, "Attempting to re-request session even harder with Server's RP-Version!!!");
		session->target = server_target;
		err = session_thread_request_session(session, NULL);
	}
	else if(err != LABS_ERR_SUCCESS)
		QUIT(quit);

	if(err != LABS_ERR_SUCCESS)
		QUIT(quit);

	LABS_LOGI(session->log, "Session request successful");

	labs_rpcrypt_init_auth(&session->rpcrypt, session->target, session->nonce, session->connect_info.morning);

	// PS4 doesn't always react right away, sleep a bit
	labs_cond_timedwait_pred(&session->state_cond, &session->state_mutex, 10, session_check_state_pred, session);

	LABS_LOGI(session->log, "Starting ctrl");

	err = labs_ctrl_start(&session->ctrl);
	if(err != LABS_ERR_SUCCESS)
		QUIT(quit);

	err = labs_cond_timedwait_pred(&session->state_cond, &session->state_mutex, SESSION_EXPECT_CTRL_START_MS, session_check_state_pred_ctrl_start, session);
	CHECK_STOP(quit_ctrl);

	if(session->ctrl_failed)
	{
		LABS_LOGE(session->log, "Ctrl has failed while waiting for ctrl startup");
		goto ctrl_failed;
	}

	bool pin_incorrect = false;
	while(session->ctrl_login_pin_requested)
	{
		session->ctrl_login_pin_requested = false;
		if(pin_incorrect)
			LABS_LOGI(session->log, "Login PIN was incorrect, requested again by Ctrl");
		else
			LABS_LOGI(session->log, "Ctrl requested Login PIN");
		LabsEvent event = { 0 };
		event.type = LABS_EVENT_LOGIN_PIN_REQUEST;
		event.login_pin_request.pin_incorrect = pin_incorrect;
		labs_session_send_event(session, &event);
		pin_incorrect = true;
		err = labs_cond_timedwait_pred(&session->state_cond, &session->state_mutex, UINT64_MAX, session_check_state_pred_pin, session);
		CHECK_STOP(quit_ctrl);
		if(session->ctrl_failed)
		{
			LABS_LOGE(session->log, "Ctrl has failed while waiting for PIN entry");
			goto ctrl_failed;
		}

		assert(session->login_pin_entered && session->login_pin);
		LABS_LOGI(session->log, "Session received entered Login PIN, forwarding to Ctrl");
		labs_ctrl_set_login_pin(&session->ctrl, session->login_pin, session->login_pin_size);
		session->login_pin_entered = false;
		free(session->login_pin);
		session->login_pin = NULL;
		session->login_pin_size = 0;
		// wait for session id or new login pin request
		err = labs_cond_timedwait_pred(&session->state_cond, &session->state_mutex, SESSION_EXPECT_CTRL_START_MS, session_check_state_pred_ctrl_start, session);
		CHECK_STOP(quit_ctrl);
	}

	labs_socket_t *data_sock = NULL;
	if(session->rudp)
	{
		LabsErrorCode err = holepunch_session_create_offer(session->holepunch_session);
		if (err != LABS_ERR_SUCCESS)
		{
			LABS_LOGE(session->log, "!! Failed to create offer msg for data connection");
			CHECK_STOP(quit_ctrl);
		}
		LABS_LOGI(session->log, "Punching hole for data connection");
		LabsEvent event_start = { 0 };
		event_start.type = LABS_EVENT_HOLEPUNCH;
		event_start.data_holepunch.finished = false;
		labs_session_send_event(session, &event_start);
		err = labs_holepunch_session_punch_hole(session->holepunch_session, LABS_HOLEPUNCH_PORT_TYPE_DATA);
		if (err != LABS_ERR_SUCCESS)
		{
			LABS_LOGE(session->log, "!! Failed to punch hole for data connection.");
			QUIT(quit_ctrl);
		}
		LABS_LOGI(session->log, ">> Punched hole for data connection!");
		data_sock = labs_get_holepunch_sock(session->holepunch_session, LABS_HOLEPUNCH_PORT_TYPE_DATA);
		LabsEvent event_finish = { 0 };
		event_finish.type = LABS_EVENT_HOLEPUNCH;
		event_finish.data_holepunch.finished = true;
		labs_session_send_event(session, &event_finish);
		err = labs_cond_timedwait_pred(&session->state_cond, &session->state_mutex, SESSION_EXPECT_TIMEOUT_MS, session_check_state_pred_ctrl_start, session);
		CHECK_STOP(quit_ctrl);
	}

	if(!session->ctrl_session_id_received)
	{
		LABS_LOGE(session->log, "Ctrl did not receive session id");
		labs_mutex_unlock(&session->state_mutex);
		err = ctrl_message_set_fallback_session_id(&session->ctrl);
		labs_mutex_lock(&session->state_mutex);
		if(err != LABS_ERR_SUCCESS)
			goto ctrl_failed;
		ctrl_enable_features(&session->ctrl);
	}

	if(!session->ctrl_session_id_received)
	{
ctrl_failed:
		LABS_LOGE(session->log, "Ctrl has failed, shutting down");
		if(session->quit_reason == LABS_QUIT_REASON_NONE)
			session->quit_reason = LABS_QUIT_REASON_CTRL_UNKNOWN;
		QUIT(quit_ctrl);
	}

#ifdef ENABLE_SENKUSHA
	LABS_LOGI(session->log, "Starting Senkusha");

	LabsSenkusha senkusha;
	err = labs_senkusha_init(&senkusha, session);
	if(err != LABS_ERR_SUCCESS)
		QUIT(quit_ctrl);

	err = labs_senkusha_run(&senkusha, &session->mtu_in, &session->mtu_out, &session->rtt_us, data_sock);
	labs_senkusha_fini(&senkusha);
	CHECK_STOP(quit_ctrl);
	if(session->ctrl_failed)
	{
		LABS_LOGE(session->log, "Ctrl has failed since session started, exiting");
		QUIT(quit_ctrl);
	}

	if(err == LABS_ERR_SUCCESS)
		LABS_LOGI(session->log, "Senkusha completed successfully");
	else if(err == LABS_ERR_CANCELED)
		QUIT(quit_ctrl);
	else
	{
		LABS_LOGE(session->log, "Senkusha failed, but we still try to connect with fallback values");
		session->mtu_in = 1454;
		session->mtu_out = 1454;
		session->rtt_us = 1000;
		session->dontfrag = false;
	}
#endif
	if(session->rudp)
	{
		LabsErrorCode err;
		err = labs_rudp_send_switch_to_stream_connection_message(session->rudp);
		if(err != LABS_ERR_SUCCESS)
		{
			LABS_LOGE(session->log, "Failed to send switch to stream connection message");
			QUIT(quit_ctrl);
		}
		err = labs_cond_timedwait_pred(&session->state_cond, &session->state_mutex, SESSION_EXPECT_TIMEOUT_MS, session_check_state_pred_stream_connection_switch, session);
		if(!session->stream_connection_switch_received)
		{
			LABS_LOGE(session->log, "Failed to receive switch to stream connection ack!");
			QUIT(quit_ctrl);
		}
		CHECK_STOP(quit_ctrl);
		LABS_LOGI(session->log, "Received Switch to Stream Connection Ack... Switching to Stream Connection now");
	}

	err = labs_random_bytes_crypt(session->handshake_key, sizeof(session->handshake_key));
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(session->log, "Session failed to generate handshake key");
		QUIT(quit_ctrl);
	}

	err = labs_ecdh_init(&session->ecdh);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(session->log, "Session failed to initialize ECDH");
		QUIT(quit_ctrl);
	}

	labs_mutex_unlock(&session->state_mutex);
	err = labs_stream_connection_run(&session->stream_connection, data_sock);
	labs_mutex_lock(&session->state_mutex);
	if(err == LABS_ERR_DISCONNECTED)
	{
		LABS_LOGE(session->log, "Remote disconnected from StreamConnection");
		if(!strcmp(session->stream_connection.remote_disconnect_reason, "Server shutting down"))
			session->quit_reason = LABS_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN;
		else
			session->quit_reason = LABS_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED;
		session->quit_reason_str = strdup(session->stream_connection.remote_disconnect_reason);
	}
	else if(err != LABS_ERR_SUCCESS && err != LABS_ERR_CANCELED)
	{
		LABS_LOGE(session->log, "StreamConnection run failed");
		session->quit_reason = LABS_QUIT_REASON_STREAM_CONNECTION_UNKNOWN;
	}
	else
	{
		LABS_LOGI(session->log, "StreamConnection completed successfully");
		session->quit_reason = LABS_QUIT_REASON_STOPPED;
	}

	labs_mutex_unlock(&session->state_mutex);
	labs_ecdh_fini(&session->ecdh);

quit_ctrl:
	labs_ctrl_stop(&session->ctrl);
	labs_ctrl_join(&session->ctrl);
	LABS_LOGI(session->log, "Ctrl stopped");

	LabsEvent quit_event;
quit:

	LABS_LOGI(session->log, "Session has quit");
	labs_mutex_lock(&session->state_mutex);
	quit_event.type = LABS_EVENT_QUIT;
	quit_event.quit.reason = session->quit_reason;
	quit_event.quit.reason_str = session->quit_reason_str;
	labs_mutex_unlock(&session->state_mutex);
	labs_session_send_event(session, &quit_event);
	return NULL;

#undef CHECK_STOP
#undef QUIT
}

typedef struct session_response_t
{
	uint32_t error_code;
	const char *nonce;
	const char *rp_version;
	bool success;
} SessionResponse;

static void parse_session_response(SessionResponse *response, LabsHttpResponse *http_response)
{
	memset(response, 0, sizeof(SessionResponse));

	for(LabsHttpHeader *header=http_response->headers; header; header=header->next)
	{
		if(strcmp(header->key, "RP-Nonce") == 0)
			response->nonce = header->value;
		else if(strcasecmp(header->key, "RP-Version") == 0)
			response->rp_version = header->value;
		else if(strcmp(header->key, "RP-Application-Reason") == 0)
			response->error_code = (uint32_t)strtoul(header->value, NULL, 0x10);
	}

	if(http_response->code == 200)
		response->success = response->nonce != NULL;
	else
		response->success = false;
}


/**
 * @param target_out if NULL, version mismatch means to fail the entire session, otherwise report the target here
 */
static LabsErrorCode session_thread_request_session(LabsSession *session, LabsTarget *target_out)
{
	labs_socket_t session_sock = LABS_INVALID_SOCKET;
	uint16_t remote_counter = 0;
	if(session->rudp)
	{
		LABS_LOGI(session->log, "SESSION START THREAD - Starting RUDP session");
		RudpMessage message;
		LabsErrorCode err = labs_rudp_send_recv(session->rudp, &message, NULL, 0, 0, INIT_REQUEST, INIT_RESPONSE, 8, 3);
		if(err != LABS_ERR_SUCCESS)
		{
			LABS_LOGE(session->log, "SESSION START THREAD - Failed to init rudp");
			return err;
		}
		size_t init_response_size = message.data_size - 8;
		uint8_t init_response[init_response_size];
		memcpy(init_response, message.data + 8, init_response_size);
		labs_rudp_message_pointers_free(&message);
		err = labs_rudp_send_recv(session->rudp, &message, init_response, init_response_size, 0, COOKIE_REQUEST, COOKIE_RESPONSE, 2, 3);
		if(err != LABS_ERR_SUCCESS)
		{
			LABS_LOGE(session->log, "SESSION START THREAD - Failed to pass rudp cookie");
			return err;
		}
		remote_counter = message.remote_counter;
		labs_rudp_message_pointers_free(&message);
	}
	else
	{
		for(struct addrinfo *ai=session->connect_info.host_addrinfos; ai; ai=ai->ai_next)
		{
			//if(ai->ai_protocol != IPPROTO_TCP)
			//	continue;

			struct sockaddr *sa = malloc(ai->ai_addrlen);
			if(!sa)
				continue;
			memcpy(sa, ai->ai_addr, ai->ai_addrlen);

			if(sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
			{
				free(sa);
				continue;
			}

			set_port(sa, htons(SESSION_PORT));

			// TODO: this can block, make cancelable somehow
			int r = getnameinfo(sa, (socklen_t)ai->ai_addrlen, session->connect_info.hostname, sizeof(session->connect_info.hostname), NULL, 0, NI_NUMERICHOST);
			if(r != 0)
			{
				LABS_LOGE(session->log, "getnameinfo failed with %s, filling the hostname with fallback", gai_strerror(r));
				memcpy(session->connect_info.hostname, "unknown", 8);
			}

			LABS_LOGI(session->log, "Trying to request session from %s:%d", session->connect_info.hostname, SESSION_PORT);

			session_sock = socket(ai->ai_family, SOCK_STREAM, 0);
			if(LABS_SOCKET_IS_INVALID(session_sock))
			{
#ifdef _WIN32
				LABS_LOGE(session->log, "Failed to create socket to request session");
#else
				LABS_LOGE(session->log, "Failed to create socket to request session: %s", strerror(errno));
#endif
				free(sa);
				continue;
			}

			LabsErrorCode err = labs_socket_set_nonblock(session_sock, true);
			if(err != LABS_ERR_SUCCESS)
			{
				LABS_LOGE(session->log, "Failed to set session socket to non-blocking: %s", labs_error_string(err));
				LABS_SOCKET_CLOSE(session_sock);
				session_sock = LABS_INVALID_SOCKET;
				free(sa);
				continue;
			}

			labs_mutex_unlock(&session->state_mutex);
			err = labs_stop_pipe_connect(&session->stop_pipe, session_sock, sa, ai->ai_addrlen, 5000);
			labs_mutex_lock(&session->state_mutex);
			if(err == LABS_ERR_CANCELED)
			{
				LABS_LOGI(session->log, "Session stopped while connecting for session request");
				session->quit_reason = LABS_QUIT_REASON_STOPPED;
				if(!LABS_SOCKET_IS_INVALID(session_sock))
				{
					LABS_SOCKET_CLOSE(session_sock);
					session_sock = LABS_INVALID_SOCKET;
				}
				free(sa);
				break;
			}
			else if(err != LABS_ERR_SUCCESS)
			{
				LABS_LOGE(session->log, "Session request connect failed: %s", labs_error_string(err));
				if(err == LABS_ERR_CONNECTION_REFUSED)
					session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED;
				else
					session->quit_reason = LABS_QUIT_REASON_NONE;
				if(!LABS_SOCKET_IS_INVALID(session_sock))
				{
					LABS_SOCKET_CLOSE(session_sock);
					session_sock = LABS_INVALID_SOCKET;
				}
				free(sa);
				continue;
			}

			free(sa);

			session->connect_info.host_addrinfo_selected = ai;
			break;
		}
	
		if(LABS_SOCKET_IS_INVALID(session_sock))
		{
			LABS_LOGE(session->log, "Session request connect failed eventually.");
			if(session->quit_reason == LABS_QUIT_REASON_NONE)
				session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
			return LABS_ERR_NETWORK;
		}
		else
			LABS_LOGI(session->log, "Connected to %s:%d", session->connect_info.hostname, SESSION_PORT);
	}

	static const char session_request_fmt[] =
			"GET %s HTTP/1.1\r\n"
			"Host: %s:%d\r\n"
			"User-Agent: remoteplay Windows\r\n"
			"Connection: close\r\n"
			"Content-Length: 0\r\n"
			"RP-Registkey: %s\r\n"
			"Rp-Version: %s\r\n"
			"\r\n";
	const char *path;
	if(session->target == LABS_TARGET_PS4_8 || session->target == LABS_TARGET_PS4_9)
		path = "/sce/rp/session";
	else if(labs_target_is_ps5(session->target))
		path = "/sie/ps5/rp/sess/init";
	else
		path = "/sie/ps4/rp/sess/init";

	size_t regist_key_len = sizeof(session->connect_info.regist_key);
	for(size_t i=0; i<regist_key_len; i++)
	{
		if(!session->connect_info.regist_key[i])
		{
			regist_key_len = i;
			break;
		}
	}
	char regist_key_hex[sizeof(session->connect_info.regist_key) * 2 + 1];
	LabsErrorCode err = format_hex(regist_key_hex, sizeof(regist_key_hex), (uint8_t *)session->connect_info.regist_key, regist_key_len);
	if(err != LABS_ERR_SUCCESS)
	{
		if(!LABS_SOCKET_IS_INVALID(session_sock))
		{
			LABS_SOCKET_CLOSE(session_sock);
			session_sock = LABS_INVALID_SOCKET;
		}
		session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
		return LABS_ERR_UNKNOWN;
	}

	const char *rp_version_str = labs_rp_version_string(session->target);
	if(!rp_version_str)
	{
		LABS_LOGE(session->log, "Failed to get version for target, probably invalid target value");
		session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
		return LABS_ERR_INVALID_DATA;
	}

	char send_buf[512];
	int port = SESSION_PORT;
	if(session->holepunch_session)
	{
		labs_get_ps_selected_addr(session->holepunch_session, session->connect_info.hostname);
		port = labs_get_ps_ctrl_port(session->holepunch_session);
	}
	int request_len = snprintf(send_buf, sizeof(send_buf), session_request_fmt,
			path, session->connect_info.hostname, port, regist_key_hex, rp_version_str);
	if(request_len < 0 || request_len >= sizeof(send_buf))
	{
		if(!LABS_SOCKET_IS_INVALID(session_sock))
		{
			LABS_SOCKET_CLOSE(session_sock);
			session_sock = LABS_INVALID_SOCKET;
		}
		session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
		return LABS_ERR_UNKNOWN;
	}

	LABS_LOGI(session->log, "Sending session request");
	labs_log_hexdump(session->log, LABS_LOG_VERBOSE, (uint8_t *)send_buf, request_len);
	if(!session->rudp)
	{
		labs_mutex_unlock(&session->state_mutex);
		err = labs_send_fully(&session->stop_pipe, session_sock, (const uint8_t *)send_buf, (size_t)request_len, SESSION_EXPECT_TIMEOUT_MS);
		LabsErrorCode mutex_err = labs_mutex_lock(&session->state_mutex);
		assert(mutex_err == LABS_ERR_SUCCESS);
		if(err != LABS_ERR_SUCCESS)
		{
			if(err == LABS_ERR_CANCELED)
				LABS_LOGI(session->log, "Session stopped while sending session request");
			else
				LABS_LOGE(session->log, "Failed to send session request");
			if(!LABS_SOCKET_IS_INVALID(session_sock))
			{
				LABS_SOCKET_CLOSE(session_sock);
				session_sock = LABS_INVALID_SOCKET;
			}
			session->quit_reason = err == LABS_ERR_CANCELED
				? LABS_QUIT_REASON_STOPPED
				: LABS_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
			return err;
		}
	}
	char buf[512];
	size_t header_size;
	size_t received_size;
	labs_mutex_unlock(&session->state_mutex);
	if(session->rudp)
		err = labs_send_recv_http_header_psn(session->rudp, session->log, &remote_counter, send_buf, request_len, buf, sizeof(buf), &header_size, &received_size);
	else
		err = labs_recv_http_header(session_sock, buf, sizeof(buf), &header_size, &received_size, &session->stop_pipe, SESSION_EXPECT_TIMEOUT_MS);
	LabsErrorCode mutex_err = labs_mutex_lock(&session->state_mutex);
	assert(mutex_err == LABS_ERR_SUCCESS);
	if(err != LABS_ERR_SUCCESS)
	{
		if(err == LABS_ERR_CANCELED)
		{
			session->quit_reason = LABS_QUIT_REASON_STOPPED;
		}
		else
		{
			LABS_LOGE(session->log, "Failed to receive session request response");
			session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
		}
		if(!LABS_SOCKET_IS_INVALID(session_sock))
		{
			LABS_SOCKET_CLOSE(session_sock);
			session_sock = LABS_INVALID_SOCKET;
		}
		return LABS_ERR_NETWORK;
	}
	if(session->rudp)
	{
		RudpMessage message;
		err = labs_rudp_send_recv(session->rudp, &message, NULL, 0, remote_counter, ACK, FINISH, 0, 3);
		if(err != LABS_ERR_SUCCESS)
			LABS_LOGW(session->log, "SESSION START THREAD - Failed to finish rudp, continuing...");
		else
			labs_rudp_message_pointers_free(&message);
	}

	LabsHttpResponse http_response;
	LABS_LOGV(session->log, "Session Response Header:");
	labs_log_hexdump(session->log, LABS_LOG_VERBOSE, (const uint8_t *)buf, header_size);
	err = labs_http_response_parse(&http_response, buf, header_size);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(session->log, "Failed to parse session request response");
		if(!LABS_SOCKET_IS_INVALID(session_sock))
		{
			LABS_SOCKET_CLOSE(session_sock);
			session_sock = LABS_INVALID_SOCKET;
		}
		session_sock = LABS_INVALID_SOCKET;
		session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
		return LABS_ERR_NETWORK;
	}

	SessionResponse response;
	parse_session_response(&response, &http_response);

	LabsErrorCode r = LABS_ERR_UNKNOWN;
	if(response.success)
	{
		size_t nonce_len = LABS_RPCRYPT_KEY_SIZE;
		err = labs_base64_decode(response.nonce, strlen(response.nonce), session->nonce, &nonce_len);
		if(err != LABS_ERR_SUCCESS || nonce_len != LABS_RPCRYPT_KEY_SIZE)
		{
			LABS_LOGE(session->log, "Nonce invalid");
			response.success = false;
			session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
		}
		else
			r = LABS_ERR_SUCCESS;
	}
	else if((response.error_code == LABS_RP_APPLICATION_REASON_RP_VERSION
				|| response.error_code == LABS_RP_APPLICATION_REASON_UNKNOWN)
			&& target_out && response.rp_version && strcmp(rp_version_str, response.rp_version))
	{
		LABS_LOGI(session->log, "Reported RP-Version mismatch. ours = %s, server = %s",
				rp_version_str ? rp_version_str : "", response.rp_version);
		*target_out = labs_rp_version_parse(response.rp_version, session->connect_info.ps5);
		if(!labs_target_is_unknown(*target_out))
			LABS_LOGI(session->log, "Detected Server RP-Version %s", labs_rp_version_string(*target_out));
		else if(!strcmp(response.rp_version, "5.0"))
		{
			LABS_LOGI(session->log, "Reported Server RP-Version is 5.0. This is probably nonsense, let's try with 9.0");
			*target_out = LABS_TARGET_PS4_9;
		}
		else
		{
			LABS_LOGE(session->log, "Server RP-Version is unknown");
			session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH;
		}
		r = LABS_ERR_VERSION_MISMATCH;
	}
	else
	{
		LABS_LOGE(session->log, "Reported Application Reason: %#x (%s)", (unsigned int)response.error_code, labs_rp_application_reason_string(response.error_code));
		switch(response.error_code)
		{
			case LABS_RP_APPLICATION_REASON_IN_USE:
				session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_RP_IN_USE;
				break;
			case LABS_RP_APPLICATION_REASON_CRASH:
				session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_RP_CRASH;
				break;
			case LABS_RP_APPLICATION_REASON_RP_VERSION:
				session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH;
				r = LABS_ERR_VERSION_MISMATCH;
				break;
			default:
				session->quit_reason = LABS_QUIT_REASON_SESSION_REQUEST_UNKNOWN;
				break;
		}
	}

	labs_http_response_fini(&http_response);
	if(!session->rudp)
	{
		if(!LABS_SOCKET_IS_INVALID(session_sock))
		{
			LABS_SOCKET_CLOSE(session_sock);
			session_sock = LABS_INVALID_SOCKET;
		}
	}
	return r;
}

static void regist_cb(LabsRegistEvent *event, void *user)
{
	LabsSession *session = user;
	switch(event->type)
	{
		case LABS_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
			LABS_LOGI(session->log, "%s successfully registered for Remote Play", event->registered_host->server_nickname);
			if(session->auto_regist)
			{
				LabsEvent event_auto_regist = { 0 };
				event_auto_regist.type = LABS_EVENT_REGIST;
				memcpy(&event_auto_regist.host, event->registered_host, sizeof(LabsRegisteredHost));
				labs_session_send_event(session, &event_auto_regist);
			}
			memcpy(session->connect_info.morning, event->registered_host->rp_key, sizeof(session->connect_info.morning));
			memcpy(session->connect_info.regist_key, event->registered_host->rp_regist_key, sizeof(session->connect_info.regist_key));
			if(!session->connect_info.ps5 && !session->auto_regist)
			{
				LabsEvent event_start = { 0 };
				event_start.type = LABS_EVENT_NICKNAME_RECEIVED;
				memcpy(event_start.server_nickname, event->registered_host->server_nickname, sizeof(event->registered_host->server_nickname));
				labs_session_send_event(session, &event_start);
			}
			labs_mutex_lock(&session->state_mutex); 
			session->psn_regist_succeeded = true;
			labs_mutex_unlock(&session->state_mutex);
			labs_cond_signal(&session->state_cond);
			break;
		case LABS_REGIST_EVENT_TYPE_FINISHED_CANCELED:
			LABS_LOGI(session->log, "PSN regist was canceled, exiting...");
			labs_mutex_lock(&session->state_mutex);
			session->quit_reason = LABS_QUIT_REASON_PSN_REGIST_FAILED;
			session->should_stop = true;
			labs_mutex_unlock(&session->state_mutex);
			labs_cond_signal(&session->state_cond);
			break;
		case LABS_REGIST_EVENT_TYPE_FINISHED_FAILED:
			LABS_LOGI(session->log, "PSN regist failed, exiting...");
			labs_mutex_lock(&session->state_mutex);
			session->quit_reason = LABS_QUIT_REASON_PSN_REGIST_FAILED;
			session->should_stop = true;
			labs_mutex_unlock(&session->state_mutex);
			labs_cond_signal(&session->state_cond);
		default:
			break;
	}
}

LABS_EXPORT LabsErrorCode labs_session_goto_bed(LabsSession *session)
{
	return labs_ctrl_goto_bed(&session->ctrl);
}

LABS_EXPORT LabsErrorCode labs_session_toggle_microphone(LabsSession *session, bool muted)
{
	LabsErrorCode err;
	err = ctrl_message_toggle_microphone(&session->ctrl, muted);
	return err;
}

LABS_EXPORT LabsErrorCode labs_session_connect_microphone(LabsSession *session)
{
	LabsErrorCode err;
	err = ctrl_message_connect_microphone(&session->ctrl);
	return err;
}

LABS_EXPORT LabsErrorCode labs_session_keyboard_set_text(LabsSession *session, const char *text)
{
	return labs_ctrl_keyboard_set_text(&session->ctrl, text);
}

LABS_EXPORT LabsErrorCode labs_session_keyboard_reject(LabsSession *session)
{
	return labs_ctrl_keyboard_reject(&session->ctrl);
}

LABS_EXPORT LabsErrorCode labs_session_keyboard_accept(LabsSession *session)
{
	return labs_ctrl_keyboard_accept(&session->ctrl);
}

LABS_EXPORT LabsErrorCode labs_session_go_home(LabsSession *session)
{
	LabsErrorCode err;
	err = ctrl_message_go_home(&session->ctrl);
	return err;
}
