// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/frameprocessor.h>
#include <labs/fec.h>
#include <labs/video.h>

#include <jerasure.h>

#include <string.h>
#include <assert.h>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

LABS_EXPORT void labs_stream_stats_reset(LabsStreamStats *stats)
{
	stats->frames = 0;
	stats->bytes = 0;
}

LABS_EXPORT void labs_stream_stats_frame(LabsStreamStats *stats, uint64_t size)
{
	stats->frames++;
	stats->bytes += size;
	//float br = (float)labs_stream_stats_bitrate(stats, 60) / 1000000.0f;
	//LABS_LOGD(NULL, "bitrate: %f", br);
}

LABS_EXPORT uint64_t labs_stream_stats_bitrate(LabsStreamStats *stats, uint64_t framerate)
{
	if (stats->frames == 0)
		return 0;
	return (stats->bytes * 8 * framerate) / stats->frames;
}

#define UNIT_SLOTS_MAX 512

struct labs_frame_unit_t
{
	size_t data_size;
};

LABS_EXPORT void labs_frame_processor_init(LabsFrameProcessor *frame_processor, LabsLog *log)
{
	frame_processor->log = log;
	frame_processor->frame_buf = NULL;
	frame_processor->frame_buf_size = 0;
	frame_processor->buf_size_per_unit = 0;
	frame_processor->buf_stride_per_unit = 0;
	frame_processor->units_source_expected = 0;
	frame_processor->units_fec_expected = 0;
	frame_processor->units_source_received = 0;
	frame_processor->units_fec_received = 0;
	frame_processor->unit_slots = NULL;
	frame_processor->unit_slots_size = 0;
	frame_processor->flushed = true;
	labs_stream_stats_reset(&frame_processor->stream_stats);
}

LABS_EXPORT void labs_frame_processor_fini(LabsFrameProcessor *frame_processor)
{
	free(frame_processor->frame_buf);
	free(frame_processor->unit_slots);
}

LABS_EXPORT LabsErrorCode labs_frame_processor_alloc_frame(LabsFrameProcessor *frame_processor, LabsTakionAVPacket *packet)
{
	if(packet->units_in_frame_total < packet->units_in_frame_fec)
	{
		LABS_LOGE(frame_processor->log, "Packet has units_in_frame_total < units_in_frame_fec");
		return LABS_ERR_INVALID_DATA;
	}

	frame_processor->flushed = false;
	frame_processor->units_source_expected = packet->units_in_frame_total - packet->units_in_frame_fec;
	frame_processor->units_fec_expected = packet->units_in_frame_fec;
	if(frame_processor->units_fec_expected < 1)
		frame_processor->units_fec_expected = 1;

	frame_processor->buf_size_per_unit = packet->data_size;
	if(packet->is_video && packet->unit_index < frame_processor->units_source_expected)
	{
		if(packet->data_size < 2)
		{
			LABS_LOGE(frame_processor->log, "Packet too small to read buf size extension");
			return LABS_ERR_BUF_TOO_SMALL;
		}
		frame_processor->buf_size_per_unit += ntohs(((labs_unaligned_uint16_t *)packet->data)[0]);
	}
	frame_processor->buf_stride_per_unit = ((frame_processor->buf_size_per_unit + 0xf) / 0x10) * 0x10;

	if(frame_processor->buf_size_per_unit == 0)
	{
		LABS_LOGE(frame_processor->log, "Frame Processor doesn't handle empty units");
		return LABS_ERR_BUF_TOO_SMALL;
	}

	frame_processor->units_source_received = 0;
	frame_processor->units_fec_received = 0;

	size_t unit_slots_size_required = frame_processor->units_source_expected + frame_processor->units_fec_expected;
	if(unit_slots_size_required > UNIT_SLOTS_MAX)
	{
		LABS_LOGE(frame_processor->log, "Packet suggests more than %u unit slots", UNIT_SLOTS_MAX);
		return LABS_ERR_INVALID_DATA;
	}
	if(unit_slots_size_required != frame_processor->unit_slots_size)
	{
		void *new_ptr = NULL;
		if(frame_processor->unit_slots)
		{
			new_ptr = realloc(frame_processor->unit_slots, unit_slots_size_required * sizeof(LabsFrameUnit));
			if(!new_ptr)
				free(frame_processor->unit_slots);
		}
		else
			new_ptr = malloc(unit_slots_size_required * sizeof(LabsFrameUnit));

		frame_processor->unit_slots = new_ptr;
		if(!new_ptr)
		{
			frame_processor->unit_slots_size = 0;
			return LABS_ERR_MEMORY;
		}
		else
			frame_processor->unit_slots_size = unit_slots_size_required;
	}
	memset(frame_processor->unit_slots, 0, frame_processor->unit_slots_size * sizeof(LabsFrameUnit));

	if(frame_processor->unit_slots_size > SIZE_MAX / frame_processor->buf_stride_per_unit)
		return LABS_ERR_OVERFLOW;
	size_t frame_buf_size_required = frame_processor->unit_slots_size * frame_processor->buf_stride_per_unit;
	if(frame_processor->frame_buf_size < frame_buf_size_required)
	{
		free(frame_processor->frame_buf);
		frame_processor->frame_buf = malloc(frame_buf_size_required + LABS_VIDEO_BUFFER_PADDING_SIZE);
		if(!frame_processor->frame_buf)
		{
			frame_processor->frame_buf_size = 0;
			return LABS_ERR_MEMORY;
		}
		frame_processor->frame_buf_size = frame_buf_size_required;
	}
	memset(frame_processor->frame_buf, 0, frame_buf_size_required + LABS_VIDEO_BUFFER_PADDING_SIZE);

	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_frame_processor_put_unit(LabsFrameProcessor *frame_processor, LabsTakionAVPacket *packet)
{
	if(packet->unit_index >= packet->units_in_frame_total)
	{
		LABS_LOGE(frame_processor->log, "Packet's unit index is outside frame unit count");
		return LABS_ERR_INVALID_DATA;
	}

	if(packet->unit_index >= frame_processor->unit_slots_size)
	{
		LABS_LOGE(frame_processor->log, "Packet's unit index is too high");
		return LABS_ERR_INVALID_DATA;
	}

	if(!packet->data_size)
	{
		LABS_LOGW(frame_processor->log, "Unit is empty");
		return LABS_ERR_INVALID_DATA;
	}

	if(packet->data_size > frame_processor->buf_size_per_unit)
	{
		LABS_LOGW(frame_processor->log, "Unit is bigger than pre-calculated size!");
		return LABS_ERR_INVALID_DATA;
	}

	LabsFrameUnit *unit = frame_processor->unit_slots + packet->unit_index;
	if(unit->data_size)
	{
		LABS_LOGW(frame_processor->log, "Received duplicate unit");
		return LABS_ERR_INVALID_DATA;
	}

	unit->data_size = packet->data_size;
	if(!frame_processor->flushed)
	{
		memcpy(frame_processor->frame_buf + packet->unit_index * frame_processor->buf_stride_per_unit,
				packet->data,
				packet->data_size);
	}

	if(packet->unit_index < frame_processor->units_source_expected)
		frame_processor->units_source_received++;
	else
		frame_processor->units_fec_received++;

	return LABS_ERR_SUCCESS;
}

LABS_EXPORT void labs_frame_processor_report_packet_stats(LabsFrameProcessor *frame_processor, LabsPacketStats *packet_stats)
{
	uint64_t received = frame_processor->units_source_received + frame_processor->units_fec_received;
	uint64_t expected = frame_processor->units_source_expected + frame_processor->units_fec_expected;
	labs_packet_stats_push_generation(packet_stats, received, expected - received);
}

static LabsErrorCode labs_frame_processor_fec(LabsFrameProcessor *frame_processor)
{
	LABS_LOGI(frame_processor->log, "Frame Processor received %u+%u / %u+%u units, attempting FEC",
				frame_processor->units_source_received, frame_processor->units_fec_received,
				frame_processor->units_source_expected, frame_processor->units_fec_expected);


	size_t erasures_count = (frame_processor->units_source_expected + frame_processor->units_fec_expected)
			- (frame_processor->units_source_received + frame_processor->units_fec_received);
	unsigned int *erasures = calloc(erasures_count, sizeof(unsigned int));
	if(!erasures)
		return LABS_ERR_MEMORY;

	size_t erasure_index = 0;
	for(size_t i=0; i<frame_processor->units_source_expected + frame_processor->units_fec_expected; i++)
	{
		LabsFrameUnit *slot = frame_processor->unit_slots + i;
		if(!slot->data_size)
		{
			if(erasure_index >= erasures_count)
			{
				// should never happen by design, but too scary not to check
				assert(false);
				free(erasures);
				return LABS_ERR_UNKNOWN;
			}
			erasures[erasure_index++] = (unsigned int)i;
		}
	}
	assert(erasure_index == erasures_count);

	LabsErrorCode err = labs_fec_decode(frame_processor->frame_buf,
			frame_processor->buf_size_per_unit, frame_processor->buf_stride_per_unit,
			frame_processor->units_source_expected, frame_processor->units_fec_expected,
			erasures, erasures_count);

	if(err != LABS_ERR_SUCCESS)
	{
		err = LABS_ERR_FEC_FAILED;
		LABS_LOGE(frame_processor->log, "FEC failed");
	}
	else
	{
		err = LABS_ERR_SUCCESS;
		LABS_LOGI(frame_processor->log, "FEC successful");

		// restore unit sizes
		for(size_t i=0; i<frame_processor->units_source_expected; i++)
		{
			LabsFrameUnit *slot = frame_processor->unit_slots + i;
			uint8_t *buf_ptr = frame_processor->frame_buf + frame_processor->buf_stride_per_unit * i;
			uint16_t padding = ntohs(*((labs_unaligned_uint16_t *)buf_ptr));
			if(padding >= frame_processor->buf_size_per_unit)
			{
				LABS_LOGE(frame_processor->log, "Padding in unit (%#x) is larger or equals to the whole unit size (%#llx)",
							(unsigned int)padding, frame_processor->buf_size_per_unit);
				labs_log_hexdump(frame_processor->log, LABS_LOG_DEBUG, buf_ptr, 0x50);
				continue;
			}
			slot->data_size = frame_processor->buf_size_per_unit - padding;
		}
	}

	free(erasures);
	return err;
}

LABS_EXPORT LabsFrameProcessorFlushResult labs_frame_processor_flush(LabsFrameProcessor *frame_processor, uint8_t **frame, size_t *frame_size)
{
	if(frame_processor->units_source_expected == 0 || frame_processor->flushed)
		return LABS_FRAME_PROCESSOR_FLUSH_RESULT_FAILED;

	//LABS_LOGD(NULL, "source: %u, fec: %u",
	//		frame_processor->units_source_expected,
	//		frame_processor->units_fec_expected);

	LabsFrameProcessorFlushResult result = LABS_FRAME_PROCESSOR_FLUSH_RESULT_SUCCESS;
	if(frame_processor->units_source_received < frame_processor->units_source_expected)
	{
		LabsErrorCode err = labs_frame_processor_fec(frame_processor);
		if(err == LABS_ERR_SUCCESS)
			result = LABS_FRAME_PROCESSOR_FLUSH_RESULT_FEC_SUCCESS;
		else
			result = LABS_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED;
	}

	size_t cur = 0;
	for(size_t i=0; i<frame_processor->units_source_expected; i++)
	{
		LabsFrameUnit *unit = frame_processor->unit_slots + i;
		if(!unit->data_size)
		{
			LABS_LOGW(frame_processor->log, "Missing unit %#llx", (unsigned long long)i);
			continue;
		}
		if(unit->data_size < 2)
		{
			LABS_LOGE(frame_processor->log, "Saved unit has size < 2");
			labs_log_hexdump(frame_processor->log, LABS_LOG_VERBOSE, frame_processor->frame_buf + i*frame_processor->buf_size_per_unit, 0x50);
			continue;
		}
		size_t part_size = unit->data_size - 2;
		uint8_t *buf_ptr = frame_processor->frame_buf + i*frame_processor->buf_stride_per_unit;
		memmove(frame_processor->frame_buf + cur, buf_ptr + 2, part_size);
		cur += part_size;
	}

	labs_stream_stats_frame(&frame_processor->stream_stats, (uint64_t)cur);

	*frame = frame_processor->frame_buf;
	*frame_size = cur;
	return result;
}
