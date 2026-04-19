// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/audioreceiver.h>
#include <labs/session.h>

#include <stdlib.h>
#include <string.h>

#define LABS_AUDIO_JITTER_PREFILL 3
#define LABS_AUDIO_JITTER_BUFFER_SIZE 8

static void labs_audio_receiver_frame(LabsAudioReceiver *audio_receiver, LabsSeqNum16 frame_index, bool is_haptics, uint8_t *buf, size_t buf_size);
static void labs_audio_receiver_clear_jitter_buffer(LabsAudioReceiver *audio_receiver);
static bool labs_audio_receiver_store_audio_frame_locked(LabsAudioReceiver *audio_receiver, LabsSeqNum16 frame_index, const uint8_t *buf, size_t buf_size);
static int labs_audio_receiver_find_audio_slot(const LabsAudioReceiver *audio_receiver, LabsSeqNum16 frame_index);
static int labs_audio_receiver_find_oldest_audio_slot(const LabsAudioReceiver *audio_receiver);
static int labs_audio_receiver_find_newest_audio_slot(const LabsAudioReceiver *audio_receiver);

LABS_EXPORT LabsErrorCode labs_audio_receiver_init(LabsAudioReceiver *audio_receiver, LabsSession *session, LabsPacketStats *packet_stats)
{
	audio_receiver->session = session;
	audio_receiver->log = session->log;
	audio_receiver->packet_stats = packet_stats;

	audio_receiver->frame_index_prev = 0;
	audio_receiver->next_frame_index = 0;
	audio_receiver->next_frame_index_valid = false;
	audio_receiver->playback_started = false;
	audio_receiver->frame_index_startup = true;
	audio_receiver->jitter_buffer_count = 0;
	memset(audio_receiver->jitter_buffer, 0, sizeof(audio_receiver->jitter_buffer));

	LabsErrorCode err = labs_mutex_init(&audio_receiver->mutex, false);
	if(err != LABS_ERR_SUCCESS)
		return err;

	return LABS_ERR_SUCCESS;
}

LABS_EXPORT void labs_audio_receiver_fini(LabsAudioReceiver *audio_receiver)
{
	labs_audio_receiver_clear_jitter_buffer(audio_receiver);
	labs_mutex_fini(&audio_receiver->mutex);
}

LABS_EXPORT void labs_audio_receiver_stream_info(LabsAudioReceiver *audio_receiver, LabsAudioHeader *audio_header)
{
	LabsAudioSinkHeader header_cb = NULL;
	void *header_cb_user = NULL;

	LabsErrorCode err = labs_mutex_lock(&audio_receiver->mutex);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(audio_receiver->log, "Failed to lock audio receiver mutex: %s", labs_error_string(err));
		return;
	}

	LABS_LOGI(audio_receiver->log, "Audio Header:");
	LABS_LOGI(audio_receiver->log, "  channels = %d", audio_header->channels);
	LABS_LOGI(audio_receiver->log, "  bits = %d", audio_header->bits);
	LABS_LOGI(audio_receiver->log, "  rate = %d", audio_header->rate);
	LABS_LOGI(audio_receiver->log, "  frame size = %d", audio_header->frame_size);
	LABS_LOGI(audio_receiver->log, "  unknown = %d", audio_header->unknown);

	header_cb = audio_receiver->session->audio_sink.header_cb;
	header_cb_user = audio_receiver->session->audio_sink.user;

	audio_receiver->frame_index_prev = 0;
	audio_receiver->next_frame_index = 0;
	audio_receiver->next_frame_index_valid = false;
	audio_receiver->playback_started = false;
	audio_receiver->frame_index_startup = true;
	labs_audio_receiver_clear_jitter_buffer(audio_receiver);

	if(header_cb)
		header_cb(audio_header, header_cb_user);

	err = labs_mutex_unlock(&audio_receiver->mutex);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(audio_receiver->log, "Failed to unlock audio receiver mutex: %s", labs_error_string(err));
		return;
	}
}

LABS_EXPORT void labs_audio_receiver_av_packet(LabsAudioReceiver *audio_receiver, LabsTakionAVPacket *packet)
{
	if(packet->codec != 5)
	{
		LABS_LOGE(audio_receiver->log, "Received Audio Packet with unknown Codec");
		return;
	}

	uint8_t source_units_count = labs_takion_av_packet_audio_source_units_count(packet);
	uint8_t fec_units_count = labs_takion_av_packet_audio_fec_units_count(packet);
	uint8_t unit_size = labs_takion_av_packet_audio_unit_size(packet);

	if(!packet->data_size)
	{
		LABS_LOGE(audio_receiver->log, "Audio AV Packet is empty");
		return;
	}

	if((uint16_t)fec_units_count + (uint16_t)source_units_count != packet->units_in_frame_total)
	{
		LABS_LOGE(audio_receiver->log, "Source Units + FEC Units != Total Units in Audio AV Packet");
		return;
	}

	if(packet->data_size != (size_t)unit_size * (size_t)packet->units_in_frame_total)
	{
		LABS_LOGE(audio_receiver->log, "Audio AV Packet size mismatch %#llx vs %#llx",
			(unsigned long long)packet->data_size,
			(unsigned long long)(unit_size * packet->units_in_frame_total));
		return;
	}

	if(packet->frame_index > (1 << 15))
		audio_receiver->frame_index_startup = false;

	for(size_t i = 0; i < source_units_count + fec_units_count; i++)
	{
		LabsSeqNum16 frame_index;
		if(i < source_units_count)
			frame_index = packet->frame_index + i;
		else
		{
			size_t fec_index = i - source_units_count;
			if(audio_receiver->frame_index_startup && packet->frame_index + fec_index < fec_units_count + 1)
				continue;
			frame_index = packet->frame_index - fec_units_count + fec_index;
		}

		labs_audio_receiver_frame(audio_receiver, frame_index, packet->is_haptics, packet->data + unit_size * i, unit_size);
	}

	if(audio_receiver->packet_stats)
		labs_packet_stats_push_seq(audio_receiver->packet_stats, packet->frame_index);
}

static void labs_audio_receiver_frame(LabsAudioReceiver *audio_receiver, LabsSeqNum16 frame_index, bool is_haptics, uint8_t *buf, size_t buf_size)
{
	while(true)
	{
		LabsAudioSinkFrame frame_cb = NULL;
		void *frame_cb_user = NULL;
		uint8_t *deliver_buf = NULL;
		size_t deliver_buf_size = 0;
		bool deliver_owned_buf = false;

		LabsErrorCode err = labs_mutex_lock(&audio_receiver->mutex);
		if(err != LABS_ERR_SUCCESS)
		{
			LABS_LOGE(audio_receiver->log, "Failed to lock audio receiver mutex: %s", labs_error_string(err));
			return;
		}

		if(is_haptics)
		{
			if(labs_seq_num_16_gt(frame_index, audio_receiver->frame_index_prev))
			{
				audio_receiver->frame_index_prev = frame_index;
				frame_cb = audio_receiver->session->haptics_sink.frame_cb;
				frame_cb_user = audio_receiver->session->haptics_sink.user;
				deliver_buf = buf;
				deliver_buf_size = buf_size;
			}
			err = labs_mutex_unlock(&audio_receiver->mutex);
			if(err != LABS_ERR_SUCCESS)
			{
				LABS_LOGE(audio_receiver->log, "Failed to unlock audio receiver mutex: %s", labs_error_string(err));
				return;
			}
			if(frame_cb)
				frame_cb(deliver_buf, deliver_buf_size, frame_cb_user);
			return;
		}

		if(buf)
		{
			if(audio_receiver->next_frame_index_valid && labs_seq_num_16_lt(frame_index, audio_receiver->next_frame_index))
				goto unlock_and_exit;

			if(!labs_audio_receiver_store_audio_frame_locked(audio_receiver, frame_index, buf, buf_size))
				goto unlock_and_exit;
			buf = NULL;
			buf_size = 0;
		}

		if(!audio_receiver->playback_started && audio_receiver->jitter_buffer_count >= LABS_AUDIO_JITTER_PREFILL)
		{
			int oldest = labs_audio_receiver_find_oldest_audio_slot(audio_receiver);
			if(oldest >= 0)
			{
				audio_receiver->next_frame_index = audio_receiver->jitter_buffer[oldest].frame_index;
				audio_receiver->next_frame_index_valid = true;
				audio_receiver->playback_started = true;
			}
		}

		if(audio_receiver->playback_started && audio_receiver->next_frame_index_valid)
		{
			int slot = labs_audio_receiver_find_audio_slot(audio_receiver, audio_receiver->next_frame_index);
			if(slot >= 0)
			{
				frame_cb = audio_receiver->session->audio_sink.frame_cb;
				frame_cb_user = audio_receiver->session->audio_sink.user;
				deliver_buf = audio_receiver->jitter_buffer[slot].buf;
				deliver_buf_size = audio_receiver->jitter_buffer[slot].buf_size;
				deliver_owned_buf = true;
				audio_receiver->jitter_buffer[slot].buf = NULL;
				audio_receiver->jitter_buffer[slot].buf_size = 0;
				audio_receiver->jitter_buffer[slot].occupied = false;
				audio_receiver->jitter_buffer_count--;
				audio_receiver->frame_index_prev = audio_receiver->next_frame_index;
				audio_receiver->next_frame_index++;
			}
			else if(audio_receiver->jitter_buffer_count > 0)
			{
				int oldest = labs_audio_receiver_find_oldest_audio_slot(audio_receiver);
				int newest = labs_audio_receiver_find_newest_audio_slot(audio_receiver);
				bool newer_audio_buffered = oldest >= 0
					&& labs_seq_num_16_gt(audio_receiver->jitter_buffer[oldest].frame_index, audio_receiver->next_frame_index);
				bool can_conceal_loss = false;
				if(newer_audio_buffered && newest >= 0)
				{
					if(audio_receiver->jitter_buffer_count >= LABS_AUDIO_JITTER_PREFILL)
					{
						LabsSeqNum16 required_lookahead = audio_receiver->next_frame_index + LABS_AUDIO_JITTER_PREFILL;
						can_conceal_loss = labs_seq_num_16_gt(audio_receiver->jitter_buffer[newest].frame_index, required_lookahead - 1);
					}
					else
					{
						can_conceal_loss = true;
					}
				}
				if(can_conceal_loss)
				{
					frame_cb = audio_receiver->session->audio_sink.frame_cb;
					frame_cb_user = audio_receiver->session->audio_sink.user;
					deliver_buf = NULL;
					deliver_buf_size = 0;
					audio_receiver->frame_index_prev = audio_receiver->next_frame_index;
					audio_receiver->next_frame_index++;
				}
			}
		}

unlock_and_exit:
		err = labs_mutex_unlock(&audio_receiver->mutex);
		if(err != LABS_ERR_SUCCESS)
		{
			LABS_LOGE(audio_receiver->log, "Failed to unlock audio receiver mutex: %s", labs_error_string(err));
			if(deliver_owned_buf)
				free(deliver_buf);
			return;
		}

		if(!frame_cb)
			return;

		frame_cb(deliver_buf, deliver_buf_size, frame_cb_user);
		if(deliver_owned_buf)
			free(deliver_buf);
	}
}

static void labs_audio_receiver_clear_jitter_buffer(LabsAudioReceiver *audio_receiver)
{
	for(size_t i = 0; i < LABS_AUDIO_JITTER_BUFFER_SIZE; i++)
	{
		free(audio_receiver->jitter_buffer[i].buf);
		audio_receiver->jitter_buffer[i].buf = NULL;
		audio_receiver->jitter_buffer[i].buf_size = 0;
		audio_receiver->jitter_buffer[i].occupied = false;
	}
	audio_receiver->jitter_buffer_count = 0;
}

static int labs_audio_receiver_find_audio_slot(const LabsAudioReceiver *audio_receiver, LabsSeqNum16 frame_index)
{
	for(size_t i = 0; i < LABS_AUDIO_JITTER_BUFFER_SIZE; i++)
	{
		if(audio_receiver->jitter_buffer[i].occupied && audio_receiver->jitter_buffer[i].frame_index == frame_index)
			return (int)i;
	}
	return -1;
}

static int labs_audio_receiver_find_oldest_audio_slot(const LabsAudioReceiver *audio_receiver)
{
	int oldest = -1;
	for(size_t i = 0; i < LABS_AUDIO_JITTER_BUFFER_SIZE; i++)
	{
		if(!audio_receiver->jitter_buffer[i].occupied)
			continue;
		if(oldest < 0 || labs_seq_num_16_lt(audio_receiver->jitter_buffer[i].frame_index,
			audio_receiver->jitter_buffer[oldest].frame_index))
			oldest = (int)i;
	}
	return oldest;
}

static int labs_audio_receiver_find_newest_audio_slot(const LabsAudioReceiver *audio_receiver)
{
	int newest = -1;
	for(size_t i = 0; i < LABS_AUDIO_JITTER_BUFFER_SIZE; i++)
	{
		if(!audio_receiver->jitter_buffer[i].occupied)
			continue;
		if(newest < 0 || labs_seq_num_16_gt(audio_receiver->jitter_buffer[i].frame_index,
			audio_receiver->jitter_buffer[newest].frame_index))
			newest = (int)i;
	}
	return newest;
}

static bool labs_audio_receiver_store_audio_frame_locked(LabsAudioReceiver *audio_receiver, LabsSeqNum16 frame_index, const uint8_t *buf, size_t buf_size)
{
	if(labs_audio_receiver_find_audio_slot(audio_receiver, frame_index) >= 0)
		return false;

	int free_slot = -1;
	for(size_t i = 0; i < LABS_AUDIO_JITTER_BUFFER_SIZE; i++)
	{
		if(!audio_receiver->jitter_buffer[i].occupied)
		{
			free_slot = (int)i;
			break;
		}
	}

	if(free_slot < 0)
	{
		int newest = labs_audio_receiver_find_newest_audio_slot(audio_receiver);
		if(newest < 0)
			return false;
		if(!labs_seq_num_16_lt(frame_index, audio_receiver->jitter_buffer[newest].frame_index))
			return false;
		free(audio_receiver->jitter_buffer[newest].buf);
		audio_receiver->jitter_buffer[newest].buf = NULL;
		audio_receiver->jitter_buffer[newest].buf_size = 0;
		audio_receiver->jitter_buffer[newest].occupied = false;
		audio_receiver->jitter_buffer_count--;
		free_slot = newest;
	}

	uint8_t *copy = NULL;
	if(buf_size > 0)
	{
		copy = malloc(buf_size);
		if(!copy)
			return false;
		memcpy(copy, buf, buf_size);
	}

	audio_receiver->jitter_buffer[free_slot].occupied = true;
	audio_receiver->jitter_buffer[free_slot].frame_index = frame_index;
	audio_receiver->jitter_buffer[free_slot].buf = copy;
	audio_receiver->jitter_buffer[free_slot].buf_size = buf_size;
	audio_receiver->jitter_buffer_count++;
	return true;
}
