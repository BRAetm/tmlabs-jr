// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/senkusha.h>
#include <labs/session.h>
#include <labs/random.h>
#include <labs/time.h>

#include <string.h>
#include <assert.h>

#include "utils.h"
#include "pb_utils.h"

#include <takion.pb.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <pb.h>
#include <labs/takion.h>
#include <labs/gkcrypt.h>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#endif


#define SENKUSHA_PORT 9297

#define EXPECT_TIMEOUT_MS 5000
#define CONNECT_TIMEOUT_MS 30000

#define SENKUSHA_PING_COUNT_DEFAULT 10
#define EXPECT_PONG_TIMEOUT_MS 1000

// Assuming IPv4, sizeof(ip header) + sizeof(udp header)
#define MTU_UDP_PACKET_ADD 0x1c

#define MTU_AV_PACKET_ADD LABS_TAKION_V7_AV_HEADER_SIZE_BASE

// Amount of bytes to add to AV data size for MTU pings to get the full size of the ip packet for MTU
#define MTU_PING_DATA_ADD (MTU_UDP_PACKET_ADD + MTU_AV_PACKET_ADD)

typedef enum {
	STATE_IDLE,
	STATE_TAKION_CONNECT,
	STATE_EXPECT_STREAMINFO_ACK,
	STATE_EXPECT_BANG,
	STATE_EXPECT_DATA_ACK,
	STATE_EXPECT_PROTOCOL_ACK,
	STATE_EXPECT_PONG,
	STATE_EXPECT_MTU,
	STATE_EXPECT_CLIENT_MTU_COMMAND
} SenkushaState;

static LabsErrorCode senkusha_run_rtt_test(LabsSenkusha *senkusha, uint16_t ping_test_index, uint16_t ping_count, uint64_t *rtt_us);
static LabsErrorCode senkusha_run_mtu_in_test(LabsSenkusha *senkusha, uint32_t min, uint32_t max, uint32_t retries, uint64_t timeout_ms, uint32_t *mtu);
static LabsErrorCode senkusha_run_mtu_out_test(LabsSenkusha *senkusha, uint32_t mtu_in, uint32_t min, uint32_t max, uint32_t retries, uint64_t timeout_ms, uint32_t *mtu);
static void senkusha_takion_cb(LabsTakionEvent *event, void *user);
static void senkusha_takion_data(LabsSenkusha *senkusha, LabsTakionMessageDataType data_type, uint8_t *buf, size_t buf_size);
static void senkusha_takion_data_ack(LabsSenkusha *senkusha, LabsSeqNum32 seq_num);
static void senkusha_takion_av(LabsSenkusha *senkusha, LabsTakionAVPacket *packet);
static LabsErrorCode senkusha_set_version(LabsSenkusha *senkusha);
static LabsErrorCode senkusha_send_big(LabsSenkusha *senkusha);
static LabsErrorCode senkusha_send_disconnect(LabsSenkusha *senkusha);
static LabsErrorCode senkusha_send_echo_command(LabsSenkusha *senkusha, bool enable);
static LabsErrorCode senkusha_send_mtu_command(LabsSenkusha *senkusha, tkproto_SenkushaMtuCommand *command);
static LabsErrorCode senkusha_send_client_mtu_command(LabsSenkusha *senkusha, tkproto_SenkushaClientMtuCommand *command, bool wait_for_ack);
static LabsErrorCode senkusha_send_data_wait_for_ack(LabsSenkusha *senkusha, uint8_t *buf, size_t buf_size);

LABS_EXPORT LabsErrorCode labs_senkusha_init(LabsSenkusha *senkusha, LabsSession *session)
{
	senkusha->session = session;
	senkusha->log = session->log;

	LabsErrorCode err = labs_mutex_init(&senkusha->state_mutex, false);
	if(err != LABS_ERR_SUCCESS)
		goto error;

	err = labs_cond_init(&senkusha->state_cond);
	if(err != LABS_ERR_SUCCESS)
		goto error_state_mutex;

	senkusha->state = STATE_IDLE;
	senkusha->state_finished = false;
	senkusha->state_failed = false;
	senkusha->should_stop = false;
	senkusha->data_ack_seq_num_expected = 0;
	senkusha->ping_tag = 0;
	senkusha->pong_time_us = 0;

	labs_key_state_init(&senkusha->takion.key_state);

	return LABS_ERR_SUCCESS;

error_state_mutex:
	labs_mutex_fini(&senkusha->state_mutex);
error:
	return err;
}

LABS_EXPORT void labs_senkusha_fini(LabsSenkusha *senkusha)
{
	labs_cond_fini(&senkusha->state_cond);
	labs_mutex_fini(&senkusha->state_mutex);
}

static bool state_finished_cond_check(void *user)
{
	LabsSenkusha *senkusha = user;
	return senkusha->state_finished || senkusha->should_stop;
}

LABS_EXPORT LabsErrorCode labs_senkusha_run(LabsSenkusha *senkusha, uint32_t *mtu_in, uint32_t *mtu_out, uint64_t *rtt_us, labs_socket_t *socket)
{
	LabsSession *session = senkusha->session;
	LabsErrorCode err;

	err = labs_mutex_lock(&senkusha->state_mutex);
	assert(err == LABS_ERR_SUCCESS);

#define QUIT(quit_label) do { \
		labs_mutex_unlock(&senkusha->state_mutex); \
		goto quit_label; \
	} while(0)

	if(senkusha->should_stop)
	{
		err = LABS_ERR_CANCELED;
		QUIT(quit);
	}

	LabsTakionConnectInfo takion_info;
	takion_info.log = senkusha->log;
	if(!socket)
	{
		takion_info.close_socket = true;
		takion_info.sa_len = session->connect_info.host_addrinfo_selected->ai_addrlen;
		takion_info.sa = malloc(takion_info.sa_len);
		if(!takion_info.sa)
		{
			err = LABS_ERR_MEMORY;
			QUIT(quit);
		}

		memcpy(takion_info.sa, session->connect_info.host_addrinfo_selected->ai_addr, takion_info.sa_len);
		err = set_port(takion_info.sa, htons(SENKUSHA_PORT));
		assert(err == LABS_ERR_SUCCESS);
	}
	else
		takion_info.close_socket = false;
	takion_info.ip_dontfrag = true;

	takion_info.enable_crypt = false;
	takion_info.disable_audio_video = false;
	takion_info.enable_dualsense = session->connect_info.enable_dualsense;
	takion_info.protocol_version = 7;

	takion_info.cb = senkusha_takion_cb;
	takion_info.cb_user = senkusha;

	senkusha->state = STATE_TAKION_CONNECT;
	senkusha->state_finished = false;
	senkusha->state_failed = false;

	err = labs_takion_connect(&senkusha->takion, &takion_info, socket);
	if(!socket)
		free(takion_info.sa);
	
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(session->log, "Senkusha connect failed");
		QUIT(quit);
	}

	err = labs_cond_timedwait_pred(&senkusha->state_cond, &senkusha->state_mutex, CONNECT_TIMEOUT_MS, state_finished_cond_check, senkusha);
	assert(err == LABS_ERR_SUCCESS || err == LABS_ERR_TIMEOUT);
	if(!senkusha->state_finished)
	{
		if(err == LABS_ERR_TIMEOUT)
			LABS_LOGE(session->log, "Senkusha connect timeout");

		if(senkusha->should_stop)
			err = LABS_ERR_CANCELED;
		else
			LABS_LOGE(session->log, "Senkusha Takion connect failed");

		QUIT(quit_takion);
	}

	LABS_LOGI(session->log, "Setting takion versions");

	senkusha->state = STATE_EXPECT_PROTOCOL_ACK;
	senkusha->state_finished = false;
	senkusha->state_failed = false;

	err = senkusha_set_version(senkusha);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(session->log, "Senkusha failed to set takion version");
		QUIT(quit_takion);
	}
	err = labs_cond_timedwait_pred(&senkusha->state_cond, &senkusha->state_mutex, EXPECT_TIMEOUT_MS, state_finished_cond_check, senkusha);
	assert(err == LABS_ERR_SUCCESS || err == LABS_ERR_TIMEOUT);

	if(!senkusha->state_finished)
	{
		if(err == LABS_ERR_TIMEOUT)
			LABS_LOGE(session->log, "Senkusha set takion version receive timeout");

		if(senkusha->should_stop)
			err = LABS_ERR_CANCELED;
		else
			LABS_LOGE(session->log, "Senkusha didn't receive protocol request ack");

		QUIT(quit_takion);
	}

	LABS_LOGI(session->log, "Senkusha successfully set takion version");

	LABS_LOGI(session->log, "Senkusha sending big");

	senkusha->state = STATE_EXPECT_BANG;
	senkusha->state_finished = false;
	senkusha->state_failed = false;
	err = senkusha_send_big(senkusha);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(session->log, "Senkusha failed to send big");
		QUIT(quit_takion);
	}

	err = labs_cond_timedwait_pred(&senkusha->state_cond, &senkusha->state_mutex, EXPECT_TIMEOUT_MS, state_finished_cond_check, senkusha);
	assert(err == LABS_ERR_SUCCESS || err == LABS_ERR_TIMEOUT);

	if(!senkusha->state_finished)
	{
		if(err == LABS_ERR_TIMEOUT)
			LABS_LOGE(session->log, "Senkusha bang receive timeout");

		if(senkusha->should_stop)
			err = LABS_ERR_CANCELED;
		else
			LABS_LOGE(session->log, "Senkusha didn't receive bang");

		QUIT(quit_takion);
	}

	LABS_LOGI(session->log, "Senkusha successfully received bang");

	err = senkusha_run_rtt_test(senkusha, 0, SENKUSHA_PING_COUNT_DEFAULT, rtt_us);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(senkusha->log, "Senkusha Ping Test failed");
		goto disconnect;
	}

	uint64_t mtu_timeout_ms = (*rtt_us * 5) / 1000;
	if(mtu_timeout_ms < 5)
		mtu_timeout_ms = 5;
	if(mtu_timeout_ms > 500)
		mtu_timeout_ms = 500;

	err = senkusha_run_mtu_in_test(senkusha, 576, 1454, 3, mtu_timeout_ms, mtu_in);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(senkusha->log, "Senkusha MTU in test failed");
		goto disconnect;
	}

	err = senkusha_run_mtu_out_test(senkusha, *mtu_in, 576, 1454, 3, mtu_timeout_ms, mtu_out);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(senkusha->log, "Senkusha MTU out test failed");
		goto disconnect;
	}

disconnect:
	LABS_LOGI(session->log, "Senkusha is disconnecting");

	senkusha_send_disconnect(senkusha);
	labs_mutex_unlock(&senkusha->state_mutex);

quit_takion:
	labs_takion_close(&senkusha->takion);
	LABS_LOGI(session->log, "Senkusha closed takion");
quit:
	return err;
}

static LabsErrorCode senkusha_run_rtt_test(LabsSenkusha *senkusha, uint16_t ping_test_index, uint16_t ping_count, uint64_t *rtt_us)
{
	LABS_LOGI(senkusha->log, "Senkusha Ping Test with count %u starting", (unsigned int)ping_count);

	LabsErrorCode err = senkusha_send_echo_command(senkusha, true);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(senkusha->log, "Senkusha Ping Test failed because sending echo command (true) failed");
		return err;
	}

	LABS_LOGI(senkusha->log, "Senkusha enabled echo");

	uint64_t rtt_us_acc = 0;
	uint64_t pings_successful = 0;
	for(uint16_t ping_index=0; ping_index<ping_count; ping_index++)
	{
		LABS_LOGI(senkusha->log, "Senkusha sending Ping %u of test index %u", (unsigned int)ping_index, (unsigned int)ping_test_index);

		LabsTakionAVPacket av_packet = { 0 };
		av_packet.codec = 0xff;
		av_packet.is_video = false;
		av_packet.frame_index = ping_test_index;
		av_packet.unit_index = ping_index;
		av_packet.units_in_frame_total = 0x800; // or 0

		uint8_t data[0x224];
		memset(data, 0, sizeof(data));

		size_t header_size;
		err = labs_takion_v7_av_packet_format_header(data, sizeof(data), &header_size, &av_packet);
		if(err != LABS_ERR_SUCCESS)
		{
			LABS_LOGE(senkusha->log, "Senkusha failed to format AV Header");
			return err;
		}

		uint32_t tag = labs_random_32();
		*((labs_unaligned_uint32_t *)(data + header_size + 4)) = htonl(tag);

		senkusha->state = STATE_EXPECT_PONG;
		senkusha->state_finished = false;
		senkusha->state_failed = false;
		senkusha->ping_test_index = ping_test_index;
		senkusha->ping_index = ping_index;
		senkusha->ping_tag = tag;

		uint64_t time_start_us = labs_time_now_monotonic_us();

		err = labs_takion_send_raw(&senkusha->takion, data, sizeof(data));
		if(err != LABS_ERR_SUCCESS)
		{
			LABS_LOGE(senkusha->log, "Senkusha failed to send ping");
			return err;
		}

		err = labs_cond_timedwait_pred(&senkusha->state_cond, &senkusha->state_mutex, EXPECT_PONG_TIMEOUT_MS, state_finished_cond_check, senkusha);
		assert(err == LABS_ERR_SUCCESS || err == LABS_ERR_TIMEOUT);

		if(!senkusha->state_finished)
		{
			if(err == LABS_ERR_TIMEOUT)
				LABS_LOGE(senkusha->log, "Senkusha pong receive timeout");

			if(senkusha->should_stop)
				return LABS_ERR_CANCELED;
			else
				LABS_LOGE(senkusha->log, "Senkusha failed to receive pong");

			continue;
		}

		uint64_t delta_us = senkusha->pong_time_us - time_start_us;
		rtt_us_acc += delta_us;
		pings_successful += 1;
		LABS_LOGI(senkusha->log, "Senkusha received Pong, RTT = %.3f ms", (float)delta_us * 0.001f);
	}

	err = senkusha_send_echo_command(senkusha, false);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(senkusha->log, "Senkusha Ping Test failed because sending echo command (false) failed");
		return err;
	}

	LABS_LOGI(senkusha->log, "Senkusha disabled echo");

	if(pings_successful < 1)
	{
		LABS_LOGE(senkusha->log, "Senkusha Ping test did not receive a single Pong");
		return LABS_ERR_UNKNOWN;
	}

	*rtt_us = rtt_us_acc / pings_successful;
	LABS_LOGI(senkusha->log, "Senkusha determined average RTT = %.3f ms", (float)(*rtt_us) * 0.001f);

	return LABS_ERR_SUCCESS;
}

static LabsErrorCode senkusha_run_mtu_in_test(LabsSenkusha *senkusha, uint32_t min, uint32_t max, uint32_t retries, uint64_t timeout_ms, uint32_t *mtu)
{
	LABS_LOGI(senkusha->log, "Senkusha starting MTU in test with min %u, max %u, retries %u, timeout %llu ms",
			(unsigned int)min, (unsigned int)max, (unsigned int)retries, (unsigned long long)timeout_ms);

	uint32_t cur = max;
	uint32_t request_id = 0;
	while((max - min) > 1)
	{
		bool success = false;
		for(uint32_t attempt=0; attempt<retries; attempt++)
		{
			senkusha->state = STATE_EXPECT_MTU;
			senkusha->state_finished = false;
			senkusha->state_failed = false;
			senkusha->mtu_id = ++request_id;

			tkproto_SenkushaMtuCommand mtu_cmd = { 0 };
			mtu_cmd.id = request_id;
			mtu_cmd.mtu_req = cur;
			mtu_cmd.num = 1;
			LabsErrorCode err = senkusha_send_mtu_command(senkusha, &mtu_cmd);
			if(err != LABS_ERR_SUCCESS)
			{
				LABS_LOGE(senkusha->log, "Senkusha failed to send MTU command");
				return err;
			}

			LABS_LOGI(senkusha->log, "Senkusha MTU request %u (min %u, max %u), id %u, attempt %u",
					(unsigned int)cur, (unsigned int)min, (unsigned int)max, (unsigned int)request_id, (unsigned int)attempt);

			err = labs_cond_timedwait_pred(&senkusha->state_cond, &senkusha->state_mutex, timeout_ms, state_finished_cond_check, senkusha);
			assert(err == LABS_ERR_SUCCESS || err == LABS_ERR_TIMEOUT);

			if(!senkusha->state_finished)
			{
				if(err == LABS_ERR_TIMEOUT)
				{
					LABS_LOGI(senkusha->log, "Senkusha MTU %u timeout", (unsigned int)cur);
					continue;
				}

				if(senkusha->should_stop)
					return LABS_ERR_CANCELED;
				else
					LABS_LOGE(senkusha->log, "Senkusha failed to receive MTU response");
			}

			LABS_LOGI(senkusha->log, "Senkusha MTU %u success", (unsigned int)cur);
			success = true;
			break;
		}

		if(success)
			min = cur;
		else
			max = cur;
		cur = min + (max - min) / 2;
	}

	LABS_LOGI(senkusha->log, "Senkusha determined inbound MTU %u", (unsigned int)min);
	*mtu = min;

	return LABS_ERR_SUCCESS;
}

static LabsErrorCode senkusha_run_mtu_out_test(LabsSenkusha *senkusha, uint32_t mtu_in, uint32_t min, uint32_t max, uint32_t retries, uint64_t timeout_ms, uint32_t *mtu)
{
	if(min < 8 + MTU_PING_DATA_ADD || max < min || mtu_in < min || mtu_in > max)
		return LABS_ERR_INVALID_DATA;

	LABS_LOGI(senkusha->log, "Senkusha starting MTU out test with min %u, max %u, retries %u, timeout %llu ms",
				(unsigned int)min, (unsigned int)max, (unsigned int)retries, (unsigned long long)timeout_ms);

	senkusha->state = STATE_EXPECT_CLIENT_MTU_COMMAND;
	senkusha->state_finished = false;
	senkusha->state_failed = false;
	senkusha->mtu_id = 1;

	tkproto_SenkushaClientMtuCommand client_mtu_cmd;
	client_mtu_cmd.id = senkusha->mtu_id;
	client_mtu_cmd.state = true;
	client_mtu_cmd.mtu_req = mtu_in;
	client_mtu_cmd.has_mtu_down = true;
	client_mtu_cmd.mtu_down = mtu_in;
	LabsErrorCode err = senkusha_send_client_mtu_command(senkusha, &client_mtu_cmd, false);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(senkusha->log, "Senkusha failed to send client MTU command");
		return err;
	}

	LABS_LOGI(senkusha->log, "Senkusha sent initial client MTU command");

	err = labs_cond_timedwait_pred(&senkusha->state_cond, &senkusha->state_mutex, EXPECT_TIMEOUT_MS, state_finished_cond_check, senkusha);
	assert(err == LABS_ERR_SUCCESS || err == LABS_ERR_TIMEOUT);

	if(!senkusha->state_finished)
	{
		if(err == LABS_ERR_TIMEOUT)
		{
			LABS_LOGE(senkusha->log, "Senkusha Client MTU Command from server receive timeout");
			return err;
		}

		if(senkusha->should_stop)
			return LABS_ERR_CANCELED;
		else
			LABS_LOGE(senkusha->log, "Senkusha failed to receive Client MTU command");

		return LABS_ERR_UNKNOWN;
	}

	size_t packet_buf_size = max - MTU_UDP_PACKET_ADD;
	uint8_t *packet_buf = malloc(packet_buf_size);
	if(!packet_buf)
		return LABS_ERR_MEMORY;
	memset(packet_buf, 0, MTU_AV_PACKET_ADD + 8);
	static const char padding[] = { 'C', 'H', 'I', 'A', 'K', 'I' };
	for(size_t i=0; i<packet_buf_size - (MTU_AV_PACKET_ADD + 8); i++)
		packet_buf[i + (MTU_AV_PACKET_ADD + 8)] = padding[i % sizeof(padding)];

	err = LABS_ERR_SUCCESS;

	uint32_t cur = mtu_in;
	while((max - min) > 1)
	{
		bool success = false;
		for(uint32_t attempt=0; attempt<retries; attempt++)
		{
			uint32_t tag = labs_random_32();

			senkusha->state = STATE_EXPECT_PONG;
			senkusha->state_finished = false;
			senkusha->state_failed = false;
			senkusha->ping_tag = tag;
			senkusha->ping_test_index = 0;
			senkusha->ping_index = (uint16_t)attempt;

			LabsTakionAVPacket av_packet = { 0 };
			av_packet.codec = 0xff;
			av_packet.is_video = false;
			av_packet.frame_index = senkusha->ping_test_index;
			av_packet.unit_index = senkusha->ping_index;
			av_packet.units_in_frame_total = 0x800;

			size_t header_size;
			err = labs_takion_v7_av_packet_format_header(packet_buf, packet_buf_size, &header_size, &av_packet);
			if(err != LABS_ERR_SUCCESS)
			{
				LABS_LOGE(senkusha->log, "Senkusha failed to format AV Header");
				goto beach;
			}
			assert(header_size == MTU_AV_PACKET_ADD);

			*((labs_unaligned_uint32_t *)(packet_buf + MTU_AV_PACKET_ADD)) = 0;
			*((labs_unaligned_uint32_t *)(packet_buf + MTU_AV_PACKET_ADD + 4)) = htonl(tag);

			LABS_LOGI(senkusha->log, "Senkusha MTU %u out ping attempt %u", (unsigned int)cur, (unsigned int)attempt);

			err = labs_takion_send_raw(&senkusha->takion, packet_buf, cur - MTU_UDP_PACKET_ADD);
			if(err != LABS_ERR_SUCCESS)
			{
				LABS_LOGE(senkusha->log, "Senkusha failed to send ping");
				err = LABS_ERR_TIMEOUT;
			}
			else
			{
				err = labs_cond_timedwait_pred(&senkusha->state_cond, &senkusha->state_mutex, timeout_ms, state_finished_cond_check, senkusha);
			}

			assert(err == LABS_ERR_SUCCESS || err == LABS_ERR_TIMEOUT);

			if(!senkusha->state_finished)
			{
				if(err == LABS_ERR_TIMEOUT)
				{
					LABS_LOGI(senkusha->log, "Senkusha MTU pong %u timeout", (unsigned int)cur);
					continue;
				}

				if(senkusha->should_stop)
				{
					err = LABS_ERR_CANCELED;
					goto beach;
				}
				else
					LABS_LOGE(senkusha->log, "Senkusha failed to receive MTU pong");
			}

			LABS_LOGI(senkusha->log, "Senkusha MTU ping %u success", (unsigned int)cur);
			success = true;
			break;
		}

		if(success)
			min = cur;
		else
			max = cur;
		cur = min + (max - min) / 2;
	}

	LABS_LOGI(senkusha->log, "Senkusha determined outbound MTU %u", (unsigned int)min);
	*mtu = min;

	LABS_LOGI(senkusha->log, "Senkusha sending final Client MTU Command");
	client_mtu_cmd.id = 2;
	client_mtu_cmd.state = false;
	client_mtu_cmd.mtu_req = max;
	client_mtu_cmd.has_mtu_down = true;
	client_mtu_cmd.mtu_down = mtu_in;
	err = senkusha_send_client_mtu_command(senkusha, &client_mtu_cmd, true);
	if(err != LABS_ERR_SUCCESS)
		LABS_LOGE(senkusha->log, "Senkusha failed to send client MTU command");

beach:
	free(packet_buf);
	return err;
}

static void senkusha_takion_cb(LabsTakionEvent *event, void *user)
{
	LabsSenkusha *senkusha = user;
	switch(event->type)
	{
		case LABS_TAKION_EVENT_TYPE_CONNECTED:
		case LABS_TAKION_EVENT_TYPE_DISCONNECT:
			labs_mutex_lock(&senkusha->state_mutex);
			if(senkusha->state == STATE_TAKION_CONNECT)
			{
				senkusha->state_finished = event->type == LABS_TAKION_EVENT_TYPE_CONNECTED;
				senkusha->state_failed = event->type == LABS_TAKION_EVENT_TYPE_DISCONNECT;
				labs_cond_signal(&senkusha->state_cond);
			}
			labs_mutex_unlock(&senkusha->state_mutex);
			break;
		case LABS_TAKION_EVENT_TYPE_DATA:
			senkusha_takion_data(senkusha, event->data.data_type, event->data.buf, event->data.buf_size);
			break;
		case LABS_TAKION_EVENT_TYPE_DATA_ACK:
			senkusha_takion_data_ack(senkusha, event->data_ack.seq_num);
			break;
		case LABS_TAKION_EVENT_TYPE_AV:
			senkusha_takion_av(senkusha, event->av);
			break;
		default:
			break;
	}
}

static void senkusha_takion_data(LabsSenkusha *senkusha, LabsTakionMessageDataType data_type, uint8_t *buf, size_t buf_size)
{
	if(data_type != LABS_TAKION_MESSAGE_DATA_TYPE_PROTOBUF)
		return;

	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	pb_istream_t stream = pb_istream_from_buffer(buf, buf_size);
	bool r = pb_decode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!r)
	{
		LABS_LOGE(senkusha->log, "Senkusha failed to decode data protobuf");
		return;
	}

	labs_mutex_lock(&senkusha->state_mutex);
	if(senkusha->state == STATE_EXPECT_BANG)
	{
		if(msg.type != tkproto_TakionMessage_PayloadType_BANG || !msg.has_bang_payload)
		{
			LABS_LOGE(senkusha->log, "Senkusha expected bang payload but received something else");
		}
		else
		{
			senkusha->state_finished = true;
			labs_cond_signal(&senkusha->state_cond);
		}
	}
	else if(senkusha->state == STATE_EXPECT_PROTOCOL_ACK)
	{
		if(msg.type != tkproto_TakionMessage_PayloadType_TAKIONPROTOCOLREQUESTACK || !msg.has_takion_protocol_request_ack)
		{
			LABS_LOGE(senkusha->log, "Senkusha expected protocol request ack but received something else");
		}
		else
		{
			senkusha->state_finished = true;
			labs_cond_signal(&senkusha->state_cond);
		}
	}
	else if(senkusha->state == STATE_EXPECT_CLIENT_MTU_COMMAND)
	{
		if(msg.type != tkproto_TakionMessage_PayloadType_SENKUSHA
			|| !msg.has_senkusha_payload
			|| msg.senkusha_payload.command != tkproto_SenkushaPayload_Command_CLIENT_MTU_COMMAND
			|| !msg.senkusha_payload.has_client_mtu_command
			|| msg.senkusha_payload.client_mtu_command.id != senkusha->mtu_id)
		{
			// There might be another MTU_COMMAND from the server, which we ignore, but this is not an error.
			if(msg.type != tkproto_TakionMessage_PayloadType_SENKUSHA
				|| !msg.has_senkusha_payload
				|| msg.senkusha_payload.command != tkproto_SenkushaPayload_Command_MTU_COMMAND)
			{
				LABS_LOGE(senkusha->log, "Senkusha expected Client MTU Command with matching id but received something else");
			}
		}
		else
		{
			LABS_LOGI(senkusha->log, "Senkusha received expected Client MTU Command");
			senkusha->state_finished = true;
			labs_cond_signal(&senkusha->state_cond);
		}
	}
	labs_mutex_unlock(&senkusha->state_mutex);
}

static void senkusha_takion_data_ack(LabsSenkusha *senkusha, LabsSeqNum32 seq_num)
{
	labs_mutex_lock(&senkusha->state_mutex);
	if(senkusha->state == STATE_EXPECT_DATA_ACK && senkusha->data_ack_seq_num_expected == seq_num)
	{
		senkusha->state_finished = true;
		labs_mutex_unlock(&senkusha->state_mutex);
		labs_cond_signal(&senkusha->state_cond);
	}
	else
		labs_mutex_unlock(&senkusha->state_mutex);
}

static void senkusha_takion_av(LabsSenkusha *senkusha, LabsTakionAVPacket *packet)
{
	uint64_t time_us = labs_time_now_monotonic_us();

	LabsErrorCode err = labs_mutex_lock(&senkusha->state_mutex);
	assert(err == LABS_ERR_SUCCESS);

	if(senkusha->state == STATE_EXPECT_PONG)
	{
		if(packet->is_video
			|| packet->frame_index != senkusha->ping_test_index
			|| packet->unit_index != senkusha->ping_index
			|| packet->data_size < 8)
		{
			LABS_LOGW(senkusha->log, "Senkusha received invalid Pong %u/%u, size: %#llx",
					(unsigned int)packet->frame_index, (unsigned int)packet->unit_index, (unsigned long long)packet->data_size);
			goto beach;
		}

		uint32_t tag = ntohl(*((uint32_t *)(packet->data + 4)));
		if(tag != senkusha->ping_tag)
		{
			LABS_LOGW(senkusha->log, "Senkusha received Pong with invalid tag");
			goto beach;
		}

		senkusha->pong_time_us = time_us;
		senkusha->state_finished = true;
		labs_mutex_unlock(&senkusha->state_mutex);
		labs_cond_signal(&senkusha->state_cond);
		return;
	}
	else if(senkusha->state == STATE_EXPECT_MTU)
	{
		//LABS_LOGD(senkusha->log, "Senkusha received av while expecting mtu");
		//labs_log_hexdump(senkusha->log, LABS_LOG_DEBUG, packet->data, packet->data_size);
		//LABS_LOGD(senkusha->log, "packet index: %u, frame index: %u, unit index: %u, units in frame: %u", packet->packet_index, packet->frame_index, packet->unit_index, packet->units_in_frame_total);

		if(!packet->is_video
			|| packet->frame_index != senkusha->mtu_id)
		{
			LABS_LOGW(senkusha->log, "Senkusha received invalid MTU response %u, size: %#llx, is video: %d",
					(unsigned int)packet->frame_index, (unsigned long long)packet->data_size, packet->is_video ? 1 : 0);
			goto beach;
		}

		senkusha->state_finished = true;
		labs_mutex_unlock(&senkusha->state_mutex);
		labs_cond_signal(&senkusha->state_cond);
		return;
	}

beach:
	labs_mutex_unlock(&senkusha->state_mutex);
}

static LabsErrorCode senkusha_set_version(LabsSenkusha *senkusha)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));
	List versions;
	versions.items[0] = 9;
	versions.num_items = 1;
	msg.type = tkproto_TakionMessage_PayloadType_TAKIONPROTOCOLREQUEST;
	msg.has_takion_protocol_request = true;
	msg.takion_protocol_request.supported_takion_versions.arg = &versions;
	msg.takion_protocol_request.supported_takion_versions.funcs.encode = labs_pb_encode_list;

	uint8_t buf[12];
	size_t buf_size;

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(senkusha->log, "Senkusha set version protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}
	buf_size = stream.bytes_written;
	LabsErrorCode err = labs_takion_send_message_data(&senkusha->takion, 1, 1, buf, buf_size, NULL);
	return err;
}

static LabsErrorCode senkusha_send_big(LabsSenkusha *senkusha)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	msg.type = tkproto_TakionMessage_PayloadType_BIG;
	msg.has_big_payload = true;
	msg.big_payload.client_version = 9;
	msg.big_payload.session_key.arg = "";
	msg.big_payload.session_key.funcs.encode = labs_pb_encode_string;
	msg.big_payload.launch_spec.arg = "";
	msg.big_payload.launch_spec.funcs.encode = labs_pb_encode_string;
	msg.big_payload.encrypted_key.arg = "";
	msg.big_payload.encrypted_key.funcs.encode = labs_pb_encode_string;

	uint8_t buf[12];
	size_t buf_size;

	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(senkusha->log, "Senkusha big protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	buf_size = stream.bytes_written;
	LabsErrorCode err = labs_takion_send_message_data(&senkusha->takion, 1, 1, buf, buf_size, NULL);

	return err;
}

static LabsErrorCode senkusha_send_disconnect(LabsSenkusha *senkusha)
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
		LABS_LOGE(senkusha->log, "Senkusha disconnect protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	buf_size = stream.bytes_written;
	LabsErrorCode err = labs_takion_send_message_data(&senkusha->takion, 1, 1, buf, buf_size, NULL);

	return err;
}

static LabsErrorCode senkusha_send_echo_command(LabsSenkusha *senkusha, bool enable)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	msg.type = tkproto_TakionMessage_PayloadType_SENKUSHA;
	msg.has_senkusha_payload = true;
	msg.senkusha_payload.command = tkproto_SenkushaPayload_Command_ECHO_COMMAND;
	msg.senkusha_payload.has_echo_command = true;
	msg.senkusha_payload.echo_command.state = enable;

	uint8_t buf[0x10];
	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(senkusha->log, "Senkusha echo command protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	return senkusha_send_data_wait_for_ack(senkusha, buf, stream.bytes_written);
}

static LabsErrorCode senkusha_send_mtu_command(LabsSenkusha *senkusha, tkproto_SenkushaMtuCommand *command)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	msg.type = tkproto_TakionMessage_PayloadType_SENKUSHA;
	msg.has_senkusha_payload = true;
	msg.senkusha_payload.command = tkproto_SenkushaPayload_Command_MTU_COMMAND;
	msg.senkusha_payload.has_mtu_command = true;
	memcpy(&msg.senkusha_payload.mtu_command, command, sizeof(msg.senkusha_payload.mtu_command));

	uint8_t buf[0x20];
	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(senkusha->log, "Senkusha mtu command protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	return labs_takion_send_message_data(&senkusha->takion, 1, 8, buf, stream.bytes_written, NULL);
}

static LabsErrorCode senkusha_send_client_mtu_command(LabsSenkusha *senkusha, tkproto_SenkushaClientMtuCommand *command, bool wait_for_ack)
{
	tkproto_TakionMessage msg;
	memset(&msg, 0, sizeof(msg));

	msg.type = tkproto_TakionMessage_PayloadType_SENKUSHA;
	msg.has_senkusha_payload = true;
	msg.senkusha_payload.command = tkproto_SenkushaPayload_Command_CLIENT_MTU_COMMAND;
	msg.senkusha_payload.has_client_mtu_command = true;
	memcpy(&msg.senkusha_payload.client_mtu_command, command, sizeof(msg.senkusha_payload.client_mtu_command));

	uint8_t buf[0x20];
	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	bool pbr = pb_encode(&stream, tkproto_TakionMessage_fields, &msg);
	if(!pbr)
	{
		LABS_LOGE(senkusha->log, "Senkusha client mtu command protobuf encoding failed");
		return LABS_ERR_UNKNOWN;
	}

	if(!wait_for_ack)
		return labs_takion_send_message_data(&senkusha->takion, 1, 8, buf, stream.bytes_written, NULL);

	return senkusha_send_data_wait_for_ack(senkusha, buf, stream.bytes_written);
}

static LabsErrorCode senkusha_send_data_wait_for_ack(LabsSenkusha *senkusha, uint8_t *buf, size_t buf_size)
{
	senkusha->state = STATE_EXPECT_DATA_ACK;
	senkusha->state_finished = false;
	senkusha->state_failed = false;
	LabsErrorCode err = labs_takion_send_message_data(&senkusha->takion, 1, 8, buf, buf_size, &senkusha->data_ack_seq_num_expected);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(senkusha->log, "Senkusha failed to send echo command");
		return err;
	}

	err = labs_cond_timedwait_pred(&senkusha->state_cond, &senkusha->state_mutex, EXPECT_TIMEOUT_MS, state_finished_cond_check, senkusha);
	assert(err == LABS_ERR_SUCCESS || err == LABS_ERR_TIMEOUT);

	if(!senkusha->state_finished)
	{
		if(err == LABS_ERR_TIMEOUT)
			LABS_LOGE(senkusha->log, "Senkusha data ack for echo command receive timeout");

		if(senkusha->should_stop)
			err = LABS_ERR_CANCELED;
		else
			LABS_LOGE(senkusha->log, "Senkusha failed to receive data ack for echo command");
	}

	return err;
}
