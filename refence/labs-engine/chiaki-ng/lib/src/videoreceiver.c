// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/videoreceiver.h>
#include "../include/labs/session.h"

#include <string.h>

static LabsErrorCode labs_video_receiver_flush_frame(LabsVideoReceiver *video_receiver);

static void add_ref_frame(LabsVideoReceiver *video_receiver, int32_t frame)
{
	if(video_receiver->reference_frames[0] != -1)
	{
		memmove(&video_receiver->reference_frames[1], &video_receiver->reference_frames[0], sizeof(int32_t) * 15);
		video_receiver->reference_frames[0] = frame;
		return;
	}
	for(int i=15; i>=0; i--)
	{
		if(video_receiver->reference_frames[i] == -1)
		{
			video_receiver->reference_frames[i] = frame;
			return;
		}
	}
}

static bool have_ref_frame(LabsVideoReceiver *video_receiver, int32_t frame)
{
	for(int i=0; i<16; i++)
		if(video_receiver->reference_frames[i] == frame)
			return true;
	return false;
}

LABS_EXPORT void labs_video_receiver_init(LabsVideoReceiver *video_receiver, struct labs_session_t *session, LabsPacketStats *packet_stats)
{
	video_receiver->session = session;
	video_receiver->log = session->log;
	memset(video_receiver->profiles, 0, sizeof(video_receiver->profiles));
	video_receiver->profiles_count = 0;
	video_receiver->profile_cur = -1;

	video_receiver->frame_index_cur = -1;
	video_receiver->frame_index_prev = -1;
	video_receiver->frame_index_prev_complete = 0;

	labs_frame_processor_init(&video_receiver->frame_processor, video_receiver->log);
	video_receiver->packet_stats = packet_stats;

	video_receiver->frames_lost = 0;
	video_receiver->frames_lost_total = 0;
	memset(video_receiver->reference_frames, -1, sizeof(video_receiver->reference_frames));
	labs_bitstream_init(&video_receiver->bitstream, video_receiver->log, video_receiver->session->connect_info.video_profile.codec);
	labs_mutex_init(&video_receiver->waiting_for_idr_mutex, false);
	video_receiver->waiting_for_idr = false;
	labs_mutex_init(&video_receiver->frames_lost_mutex, false);
}

LABS_EXPORT void labs_video_receiver_fini(LabsVideoReceiver *video_receiver)
{
	for(size_t i=0; i<video_receiver->profiles_count; i++)
		free(video_receiver->profiles[i].header);
	labs_mutex_fini(&video_receiver->waiting_for_idr_mutex);
	labs_mutex_fini(&video_receiver->frames_lost_mutex);
	labs_frame_processor_fini(&video_receiver->frame_processor);
}

LABS_EXPORT void labs_video_receiver_set_waiting_for_idr(LabsVideoReceiver *video_receiver, bool waiting_for_idr)
{
	labs_mutex_lock(&video_receiver->waiting_for_idr_mutex);
	video_receiver->waiting_for_idr = waiting_for_idr;
	labs_mutex_unlock(&video_receiver->waiting_for_idr_mutex);
}

LABS_EXPORT bool labs_video_receiver_get_waiting_for_idr(LabsVideoReceiver *video_receiver)
{
	bool waiting_for_idr;
	labs_mutex_lock(&video_receiver->waiting_for_idr_mutex);
	waiting_for_idr = video_receiver->waiting_for_idr;
	labs_mutex_unlock(&video_receiver->waiting_for_idr_mutex);
	return waiting_for_idr;
}

LABS_EXPORT int32_t labs_video_receiver_get_frames_lost_total(LabsVideoReceiver *video_receiver)
{
	int32_t total;
	labs_mutex_lock(&video_receiver->frames_lost_mutex);
	total = video_receiver->frames_lost_total;
	labs_mutex_unlock(&video_receiver->frames_lost_mutex);
	return total;
}

LABS_EXPORT void labs_video_receiver_stream_info(LabsVideoReceiver *video_receiver, LabsVideoProfile *profiles, size_t profiles_count)
{
	if(video_receiver->profiles_count > 0)
	{
		LABS_LOGE(video_receiver->log, "Video Receiver profiles already set");
		return;
	}

	memcpy(video_receiver->profiles, profiles, profiles_count * sizeof(LabsVideoProfile));
	video_receiver->profiles_count = profiles_count;

	LABS_LOGI(video_receiver->log, "Video Profiles:");
	for(size_t i=0; i<video_receiver->profiles_count; i++)
	{
		LabsVideoProfile *profile = &video_receiver->profiles[i];
		LABS_LOGI(video_receiver->log, "  %zu: %ux%u", i, profile->width, profile->height);
		//labs_log_hexdump(video_receiver->log, LABS_LOG_DEBUG, profile->header, profile->header_sz);
	}
}

LABS_EXPORT void labs_video_receiver_av_packet(LabsVideoReceiver *video_receiver, LabsTakionAVPacket *packet)
{
	// old frame?
	LabsSeqNum16 frame_index = packet->frame_index;
	LabsErrorCode err = LABS_ERR_SUCCESS;
	if(video_receiver->frame_index_cur >= 0
		&& labs_seq_num_16_lt(frame_index, (LabsSeqNum16)video_receiver->frame_index_cur))
	{
		LABS_LOGW(video_receiver->log, "Video Receiver received old frame packet");
		return;
	}

	// check adaptive stream index
	if(video_receiver->profile_cur < 0 || video_receiver->profile_cur != packet->adaptive_stream_index)
	{
		if(packet->adaptive_stream_index >= video_receiver->profiles_count)
		{
			LABS_LOGE(video_receiver->log, "Packet has invalid adaptive stream index %u >= %u",
					(unsigned int)packet->adaptive_stream_index,
					(unsigned int)video_receiver->profiles_count);
			return;
		}
		video_receiver->profile_cur = packet->adaptive_stream_index;

		LabsVideoProfile *profile = video_receiver->profiles + video_receiver->profile_cur;
		LABS_LOGI(video_receiver->log, "Switched to profile %d, resolution: %ux%u", video_receiver->profile_cur, profile->width, profile->height);
		if(video_receiver->session->video_sample_cb)
			video_receiver->session->video_sample_cb(profile->header, profile->header_sz, 0, false, video_receiver->session->video_sample_cb_user);
		if(!labs_bitstream_header(&video_receiver->bitstream, profile->header, profile->header_sz))
			LABS_LOGW(video_receiver->log, "Failed to parse video header");
	}

	// next frame?
	if(video_receiver->frame_index_cur < 0 ||
		labs_seq_num_16_gt(frame_index, (LabsSeqNum16)video_receiver->frame_index_cur))
	{
		if(video_receiver->packet_stats)
			labs_frame_processor_report_packet_stats(&video_receiver->frame_processor, video_receiver->packet_stats);

		// last frame not flushed yet?
		if(video_receiver->frame_index_cur >= 0 && video_receiver->frame_index_prev != video_receiver->frame_index_cur)
			err = labs_video_receiver_flush_frame(video_receiver);

		if(err != LABS_ERR_SUCCESS)
			LABS_LOGW(video_receiver->log, "Video receiver could not flush frame.");

		LabsSeqNum16 next_frame_expected = (LabsSeqNum16)(video_receiver->frame_index_prev_complete + 1);
		if(labs_seq_num_16_gt(frame_index, next_frame_expected)
			&& !(frame_index == 1 && video_receiver->frame_index_cur < 0)) // ok for frame 1
		{
			LABS_LOGW(video_receiver->log, "Detected missing or corrupt frame(s) from %d to %d", next_frame_expected, (int)frame_index);
			err = stream_connection_send_corrupt_frame(&video_receiver->session->stream_connection, next_frame_expected, frame_index - 1);
			if(err != LABS_ERR_SUCCESS)
				LABS_LOGW(video_receiver->log, "Error sending corrupt frame.");
		}

		video_receiver->frame_index_cur = frame_index;
		err = labs_frame_processor_alloc_frame(&video_receiver->frame_processor, packet);
		if(err != LABS_ERR_SUCCESS)
			LABS_LOGW(video_receiver->log, "Video receiver could not allocate frame for packet.");
	}

	err = labs_frame_processor_put_unit(&video_receiver->frame_processor, packet);
	if(err != LABS_ERR_SUCCESS)
		LABS_LOGW(video_receiver->log, "Video receiver could not put unit.");

	// if we are currently building up a frame
	if(video_receiver->frame_index_cur != video_receiver->frame_index_prev)
	{
		// if we already have enough for the whole frame, flush it already
		if(labs_frame_processor_flush_possible(&video_receiver->frame_processor) || packet->unit_index == packet->units_in_frame_total - 1)
			err = labs_video_receiver_flush_frame(video_receiver);
		if(err != LABS_ERR_SUCCESS)
			LABS_LOGW(video_receiver->log, "Video receiver could not flush frame.");
	}
}

static LabsErrorCode labs_video_receiver_flush_frame(LabsVideoReceiver *video_receiver)
{
	uint8_t *frame;
	size_t frame_size;
	LabsFrameProcessorFlushResult flush_result = labs_frame_processor_flush(&video_receiver->frame_processor, &frame, &frame_size);

	if(flush_result == LABS_FRAME_PROCESSOR_FLUSH_RESULT_FAILED
		|| flush_result == LABS_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED)
	{
		if (flush_result == LABS_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED)
		{
			LabsSeqNum16 next_frame_expected = (LabsSeqNum16)(video_receiver->frame_index_prev_complete + 1);
			stream_connection_send_corrupt_frame(&video_receiver->session->stream_connection, next_frame_expected, video_receiver->frame_index_cur);
			if(video_receiver->session->connect_info.enable_idr_on_fec_failure)
			{
				bool waiting_for_idr = labs_video_receiver_get_waiting_for_idr(video_receiver);
				bool idr_request_sent = waiting_for_idr;
				if(!waiting_for_idr)
				{
					LabsErrorCode err = stream_connection_send_idr_request(&video_receiver->session->stream_connection);
					idr_request_sent = err == LABS_ERR_SUCCESS;
					if(err == LABS_ERR_SUCCESS)
					{
						labs_video_receiver_set_waiting_for_idr(video_receiver, true);
						LABS_LOGI(video_receiver->log, "FEC failed, waiting for IDR frame");
					}
					else
					{
						LABS_LOGW(video_receiver->log, "FEC failed and IDR request could not be sent: %s", labs_error_string(err));
					}
				}
				else
				{
					LABS_LOGW(video_receiver->log, "Video FEC failure, already waiting for requested IDR");
				}
				LabsEvent event = { 0 };
				event.type = LABS_EVENT_VIDEO_FEC_FAILURE;
				event.video_fec_failure.frame_index = video_receiver->frame_index_cur;
				event.video_fec_failure.idr_request_sent = idr_request_sent;
				labs_session_send_event(video_receiver->session, &event);
			}
		int32_t lost = video_receiver->frame_index_cur - next_frame_expected + 1;
		labs_mutex_lock(&video_receiver->frames_lost_mutex);
		video_receiver->frames_lost += lost;
		video_receiver->frames_lost_total += lost;
		labs_mutex_unlock(&video_receiver->frames_lost_mutex);
		video_receiver->frame_index_prev = video_receiver->frame_index_cur;
	}
		LABS_LOGW(video_receiver->log, "Failed to complete frame %d", (int)video_receiver->frame_index_cur);
		return LABS_ERR_UNKNOWN;
	}

	bool succ = flush_result != LABS_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED;
	bool recovered = false;

	LabsBitstreamSlice slice;
	if(labs_bitstream_slice(&video_receiver->bitstream, frame, frame_size, &slice))
	{
		if(labs_video_receiver_get_waiting_for_idr(video_receiver))
		{
			if(slice.slice_type == LABS_BITSTREAM_SLICE_I)
			{
				labs_video_receiver_set_waiting_for_idr(video_receiver, false);
				LABS_LOGI(video_receiver->log, "Received IDR frame, resuming decode");
			}
			else
			{
				LABS_LOGV(video_receiver->log, "Skipping P-frame %d while waiting for IDR", (int)video_receiver->frame_index_cur);
				video_receiver->frame_index_prev = video_receiver->frame_index_cur;
				return LABS_ERR_SUCCESS;
			}
		}

		if(slice.slice_type == LABS_BITSTREAM_SLICE_P)
		{
			LabsSeqNum16 ref_frame_index = video_receiver->frame_index_cur - slice.reference_frame - 1;
			if(slice.reference_frame != 0xff && !have_ref_frame(video_receiver, ref_frame_index))
			{
				for(unsigned i=slice.reference_frame+1; i<16; i++)
				{
					LabsSeqNum16 ref_frame_index_new = video_receiver->frame_index_cur - i - 1;
					if(have_ref_frame(video_receiver, ref_frame_index_new))
					{
						if(labs_bitstream_slice_set_reference_frame(&video_receiver->bitstream, frame, frame_size, i))
						{
							recovered = true;
							LABS_LOGW(video_receiver->log, "Missing reference frame %d for decoding frame %d -> changed to %d", (int)ref_frame_index, (int)video_receiver->frame_index_cur, (int)ref_frame_index_new);
						}
						break;
					}
				}
				if(!recovered)
				{
					succ = false;
					labs_mutex_lock(&video_receiver->frames_lost_mutex);
					video_receiver->frames_lost++;
					video_receiver->frames_lost_total++;
					labs_mutex_unlock(&video_receiver->frames_lost_mutex);
					LABS_LOGW(video_receiver->log, "Missing reference frame %d for decoding frame %d", (int)ref_frame_index, (int)video_receiver->frame_index_cur);
				}
			}
		}
	}

	if(succ && video_receiver->session->video_sample_cb)
	{
		bool cb_succ = video_receiver->session->video_sample_cb(frame, frame_size, video_receiver->frames_lost, recovered, video_receiver->session->video_sample_cb_user);
		labs_mutex_lock(&video_receiver->frames_lost_mutex);
		video_receiver->frames_lost = 0;
		labs_mutex_unlock(&video_receiver->frames_lost_mutex);
		if(!cb_succ)
		{
			succ = false;
			LABS_LOGW(video_receiver->log, "Video callback did not process frame successfully.");
		}
		else
		{
			add_ref_frame(video_receiver, video_receiver->frame_index_cur);
			LABS_LOGV(video_receiver->log, "Added reference %c frame %d", slice.slice_type == LABS_BITSTREAM_SLICE_I ? 'I' : 'P', (int)video_receiver->frame_index_cur);
		}
	}

	video_receiver->frame_index_prev = video_receiver->frame_index_cur;

	if(succ)
		video_receiver->frame_index_prev_complete = video_receiver->frame_index_cur;

	return LABS_ERR_SUCCESS;
}
