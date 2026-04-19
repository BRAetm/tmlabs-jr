// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_FRAMEPROCESSOR_H
#define LABS_FRAMEPROCESSOR_H

#include "common.h"
#include "takion.h"
#include "packetstats.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_stream_stats_t
{
	uint64_t frames;
	uint64_t bytes;
} LabsStreamStats;

LABS_EXPORT void labs_stream_stats_reset(LabsStreamStats *stats);
LABS_EXPORT void labs_stream_stats_frame(LabsStreamStats *stats, uint64_t size);
LABS_EXPORT uint64_t labs_stream_stats_bitrate(LabsStreamStats *stats, uint64_t framerate);

struct labs_frame_unit_t;
typedef struct labs_frame_unit_t LabsFrameUnit;

typedef struct labs_frame_processor_t
{
	LabsLog *log;
	uint8_t *frame_buf;
	size_t frame_buf_size;
	size_t buf_size_per_unit;
	size_t buf_stride_per_unit;
	unsigned int units_source_expected;
	unsigned int units_fec_expected;
	unsigned int units_source_received;
	unsigned int units_fec_received;
	LabsFrameUnit *unit_slots;
	size_t unit_slots_size;
	bool flushed; // whether we have already flushed the current frame, i.e. are only interested in stats, not data.
	LabsStreamStats stream_stats;
} LabsFrameProcessor;

typedef enum labs_frame_flush_result_t {
	LABS_FRAME_PROCESSOR_FLUSH_RESULT_SUCCESS = 0,
	LABS_FRAME_PROCESSOR_FLUSH_RESULT_FEC_SUCCESS = 1,
	LABS_FRAME_PROCESSOR_FLUSH_RESULT_FEC_FAILED = 2,
	LABS_FRAME_PROCESSOR_FLUSH_RESULT_FAILED = 3
} LabsFrameProcessorFlushResult;

LABS_EXPORT void labs_frame_processor_init(LabsFrameProcessor *frame_processor, LabsLog *log);
LABS_EXPORT void labs_frame_processor_fini(LabsFrameProcessor *frame_processor);

LABS_EXPORT void labs_frame_processor_report_packet_stats(LabsFrameProcessor *frame_processor, LabsPacketStats *packet_stats);
LABS_EXPORT LabsErrorCode labs_frame_processor_alloc_frame(LabsFrameProcessor *frame_processor, LabsTakionAVPacket *packet);
LABS_EXPORT LabsErrorCode labs_frame_processor_put_unit(LabsFrameProcessor *frame_processor, LabsTakionAVPacket *packet);

/**
 * @param frame unless LABS_FRAME_PROCESSOR_FLUSH_RESULT_FAILED returned, will receive a pointer into the internal buffer of frame_processor.
 * MUST NOT be used after the next call to this frame processor!
 */
LABS_EXPORT LabsFrameProcessorFlushResult labs_frame_processor_flush(LabsFrameProcessor *frame_processor, uint8_t **frame, size_t *frame_size);

static inline bool labs_frame_processor_flush_possible(LabsFrameProcessor *frame_processor)
{
	return frame_processor->units_source_received + frame_processor->units_fec_received
		>= frame_processor->units_source_expected;
}

#ifdef __cplusplus
}
#endif

#endif // LABS_FRAMEPROCESSOR_H
