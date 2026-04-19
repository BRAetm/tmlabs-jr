// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_UNIT_TEST

#include <labs/remote/rudpsendbuffer.h>
#include <labs/time.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#define RUDP_DATA_RESEND_TIMEOUT_MS 400
#define RUDP_DATA_RESEND_WAKEUP_TIMEOUT_MS (RUDP_DATA_RESEND_TIMEOUT_MS/2)
#define RUDP_DATA_RESEND_TRIES_MAX 25
#define RUDP_SEND_BUFFER_SIZE 16

#endif

struct labs_rudp_send_buffer_packet_t
{
	LabsSeqNum16 seq_num;
	uint64_t tries;
	uint64_t last_send_ms; // labs_time_now_monotonic_ms()
	uint8_t *buf;
	size_t buf_size;
}; // LabsRudpSendBufferPacket

#ifndef LABS_UNIT_TEST

static void *rudp_send_buffer_thread_func(void *user);

LABS_EXPORT LabsErrorCode labs_rudp_send_buffer_init(LabsRudpSendBuffer *send_buffer, LabsRudp rudp, LabsLog *log, size_t size)
{
	LabsErrorCode err = labs_mutex_init(&send_buffer->mutex, false);
	if(err != LABS_ERR_SUCCESS)
		return err;
	labs_mutex_lock(&send_buffer->mutex);
	send_buffer->rudp = rudp;
	send_buffer->log = log;

	send_buffer->packets = calloc(size, sizeof(LabsRudpSendBufferPacket));
	if(!send_buffer->packets)
	{
		labs_mutex_unlock(&send_buffer->mutex);
		err = LABS_ERR_MEMORY;
		goto error_mutex;
	}
	send_buffer->packets_size = size;
	send_buffer->packets_count = 0;

	send_buffer->should_stop = false;
	labs_mutex_unlock(&send_buffer->mutex);
	err = labs_cond_init(&send_buffer->cond);
	if(err != LABS_ERR_SUCCESS)
		goto error_packets;

	err = labs_thread_create(&send_buffer->thread, rudp_send_buffer_thread_func, send_buffer);
	if(err != LABS_ERR_SUCCESS)
		goto error_cond;

	labs_thread_set_name(&send_buffer->thread, "Labs Rudp Send Buffer");

	return LABS_ERR_SUCCESS;
error_cond:
	labs_cond_fini(&send_buffer->cond);
error_packets:
	free(send_buffer->packets);
error_mutex:
	labs_mutex_fini(&send_buffer->mutex);
	return err;
}

LABS_EXPORT void labs_rudp_send_buffer_fini(LabsRudpSendBuffer *send_buffer)
{
	send_buffer->should_stop = true;
	LabsErrorCode err = labs_cond_signal(&send_buffer->cond);
	assert(err == LABS_ERR_SUCCESS);
	err = labs_thread_join(&send_buffer->thread, NULL);
	assert(err == LABS_ERR_SUCCESS);

	for(size_t i=0; i<send_buffer->packets_count; i++)
		free(send_buffer->packets[i].buf);

	labs_cond_fini(&send_buffer->cond);
	labs_mutex_fini(&send_buffer->mutex);
	free(send_buffer->packets);
}

static void GetRudpPacketType(LabsRudpSendBuffer *send_buffer, uint16_t packet_type, char *ptype)
{
    RudpPacketType type = htons(packet_type);
    switch(type)
    {
        case INIT_REQUEST:
			strcpy(ptype, "Init Request");
			break;
        case INIT_RESPONSE:
            strcpy(ptype, "Init Response");
			break;
        case COOKIE_REQUEST:
			strcpy(ptype, "Cookie Request");
            break;
        case COOKIE_RESPONSE:
            strcpy(ptype, "Cookie Response");
			break;
        case SESSION_MESSAGE:
			strcpy(ptype, "Session Message");
            break;
        case STREAM_CONNECTION_SWITCH_ACK:
			strcpy(ptype, "Takion Switch Ack");
            break;
        case ACK:
            strcpy(ptype, "Ack");
			break;
        case CTRL_MESSAGE:
            strcpy(ptype, "Ctrl Message");
			break;
        case UNKNOWN:
            strcpy(ptype, "Unknown");
			break;
        case FINISH:
			strcpy(ptype, "Finish");
            break;
        default:
			sprintf(ptype, "Undefined packet type 0x%04x", type);
			break;
    }
}

LABS_EXPORT LabsErrorCode labs_rudp_send_buffer_push(LabsRudpSendBuffer *send_buffer, LabsSeqNum16 seq_num, uint8_t *buf, size_t buf_size)
{
	LabsErrorCode err = labs_mutex_lock(&send_buffer->mutex);
	if(err != LABS_ERR_SUCCESS)
		return err;

	if(send_buffer->packets_count >= send_buffer->packets_size)
	{
		LABS_LOGE(send_buffer->log, "Rudp Send Buffer overflow");
		err = LABS_ERR_OVERFLOW;
		goto beach;
	}

	for(size_t i=0; i<send_buffer->packets_count; i++)
	{
		if(send_buffer->packets[i].seq_num == seq_num)
		{
			LABS_LOGE(send_buffer->log, "Tried to push duplicate seqnum into Rudp Send Buffer");
			err = LABS_ERR_INVALID_DATA;
			goto beach;
		}
	}

	LabsRudpSendBufferPacket *packet = &send_buffer->packets[send_buffer->packets_count++];
	packet->seq_num = seq_num;
	packet->tries = 0;
	packet->last_send_ms = labs_time_now_monotonic_ms();
	packet->buf = buf;
	packet->buf_size = buf_size;

	LABS_LOGV(send_buffer->log, "Pushed seq num %#lx into Rudp Send Buffer", (unsigned long)seq_num);

	if(send_buffer->packets_count == 1)
	{
		// buffer was empty before, so it will sleep without timeout => WAKE UP!!
		labs_cond_signal(&send_buffer->cond);
	}

beach:
	if(err != LABS_ERR_SUCCESS)
		free(buf);
	labs_mutex_unlock(&send_buffer->mutex);
	return err;
}

LABS_EXPORT LabsErrorCode labs_rudp_send_buffer_ack(LabsRudpSendBuffer *send_buffer, LabsSeqNum16 seq_num, LabsSeqNum16 *acked_seq_nums, size_t *acked_seq_nums_count)
{
	LabsErrorCode err = labs_mutex_lock(&send_buffer->mutex);
	if(err != LABS_ERR_SUCCESS)
		return err;

	if(acked_seq_nums_count)
		*acked_seq_nums_count = 0;

	size_t i;
	size_t shift = 0; // amount to shift back
	size_t shift_start = SIZE_MAX;
	for(i=0; i<send_buffer->packets_count; i++)
	{
		if(send_buffer->packets[i].seq_num == seq_num || labs_seq_num_16_lt(send_buffer->packets[i].seq_num, seq_num))
		{
			if(acked_seq_nums && acked_seq_nums_count)
				acked_seq_nums[(*acked_seq_nums_count)++] = send_buffer->packets[i].seq_num;

			free(send_buffer->packets[i].buf);
			if(shift_start == SIZE_MAX)
			{
				// first shift
				shift_start = i;
				shift = 1;
			}
			else if(shift_start + shift == i)
			{
				// still in the same gap
				shift++;
			}
			else
			{
				// new gap, do shift
				memmove(send_buffer->packets + shift_start,
						send_buffer->packets + shift_start + shift,
						(i - (shift_start + shift)) * sizeof(LabsRudpSendBufferPacket));
				// start new shift
				shift_start = i - shift;
				shift++;
			}
		}
	}

	if(shift_start != SIZE_MAX)
	{
		// do final shift
		if(shift_start + shift < send_buffer->packets_count)
		{
			memmove(send_buffer->packets + shift_start,
					send_buffer->packets + shift_start + shift,
					(send_buffer->packets_count - (shift_start + shift)) * sizeof(LabsRudpSendBufferPacket));
		}
		send_buffer->packets_count -= shift;
	}

	LABS_LOGV(send_buffer->log, "Acked seq num %#lx from Rudp Send Buffer", (unsigned long)seq_num);

	labs_mutex_unlock(&send_buffer->mutex);
	return err;
}

static void rudp_send_buffer_resend(LabsRudpSendBuffer *send_buffer);

static bool rudp_send_buffer_check_pred_packets(void *user)
{
	LabsRudpSendBuffer *send_buffer = user;
	return send_buffer->should_stop;
}

static bool rudp_send_buffer_check_pred_no_packets(void *user)
{
	LabsRudpSendBuffer *send_buffer = user;
	return send_buffer->should_stop || send_buffer->packets_count;
}

static void *rudp_send_buffer_thread_func(void *user)
{
	LabsRudpSendBuffer *send_buffer = user;
	labs_thread_set_affinity(LABS_THREAD_NAME_RUDP_SEND);

	LabsErrorCode err = labs_mutex_lock(&send_buffer->mutex);
	if(err != LABS_ERR_SUCCESS)
		return NULL;

	while(true)
	{
		if(send_buffer->packets_count) // if there are packets, wait with timeout
			err = labs_cond_timedwait_pred(&send_buffer->cond, &send_buffer->mutex, RUDP_DATA_RESEND_WAKEUP_TIMEOUT_MS, rudp_send_buffer_check_pred_packets, send_buffer);
		else // if not, wait without timeout, but also wakeup if packets become available
			err = labs_cond_wait_pred(&send_buffer->cond, &send_buffer->mutex, rudp_send_buffer_check_pred_no_packets, send_buffer);

		if(err != LABS_ERR_SUCCESS && err != LABS_ERR_TIMEOUT)
			break;

		if(send_buffer->should_stop)
			break;

		rudp_send_buffer_resend(send_buffer);
	}

	labs_mutex_unlock(&send_buffer->mutex);

	return NULL;
}

static void rudp_send_buffer_resend(LabsRudpSendBuffer *send_buffer)
{
	if(!send_buffer->rudp)
		return;

	uint64_t now = labs_time_now_monotonic_ms();

	for(size_t i=0; i<send_buffer->packets_count; i++)
	{
		LabsRudpSendBufferPacket *packet = &send_buffer->packets[i];
		if(now - packet->last_send_ms > RUDP_DATA_RESEND_TIMEOUT_MS)
		{
			if(packet->tries >= RUDP_DATA_RESEND_TRIES_MAX)
			{
				LABS_LOGI(send_buffer->log, "Hit max retries of %d tries giving up on packet with seqnum %#lx", RUDP_DATA_RESEND_TRIES_MAX, (unsigned long)packet->seq_num);
				LabsSeqNum16 ack_seq_nums[RUDP_SEND_BUFFER_SIZE];
				size_t ack_seq_nums_count;
				labs_mutex_unlock(&send_buffer->mutex);
				labs_rudp_send_buffer_ack(send_buffer, packet->seq_num, ack_seq_nums, &ack_seq_nums_count);
				labs_mutex_lock(&send_buffer->mutex);
				if(i > 0)
					i-= 1;
				continue;
			}
			char packet_type[29] = {0};
			GetRudpPacketType(send_buffer, *((uint16_t *)(packet->buf + 6)), packet_type);
			LABS_LOGI(send_buffer->log, "rudp Send Buffer re-sending packet with seqnum %#lx and type %s, tries: %llu", (unsigned long)packet->seq_num, packet_type, (unsigned long long)packet->tries);
			packet->last_send_ms = now;
			labs_rudp_send_raw(send_buffer->rudp, packet->buf, packet->buf_size);
			packet->tries++;
		}
	}
}

#endif