// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_VIDEORECEIVER_H
#define LABS_VIDEORECEIVER_H

#include "common.h"
#include "log.h"
#include "video.h"
#include "takion.h"
#include "frameprocessor.h"
#include "bitstream.h"
#include "thread.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LABS_VIDEO_PROFILES_MAX 8

typedef struct labs_video_receiver_t
{
	struct labs_session_t *session;
	LabsLog *log;
	LabsVideoProfile profiles[LABS_VIDEO_PROFILES_MAX];
	size_t profiles_count;
	int profile_cur; // < 1 if no profile selected yet, else index in profiles

	int32_t frame_index_cur; // frame that is currently being filled
	int32_t frame_index_prev; // last frame that has been at least partially decoded
	int32_t frame_index_prev_complete; // last frame that has been completely decoded
	LabsFrameProcessor frame_processor;
	LabsPacketStats *packet_stats;

	int32_t frames_lost;
	int32_t frames_lost_total;
	int32_t reference_frames[16];
	LabsBitstream bitstream;
	LabsMutex waiting_for_idr_mutex;
	bool waiting_for_idr;
	LabsMutex frames_lost_mutex;
} LabsVideoReceiver;

LABS_EXPORT void labs_video_receiver_init(LabsVideoReceiver *video_receiver, struct labs_session_t *session, LabsPacketStats *packet_stats);
LABS_EXPORT void labs_video_receiver_fini(LabsVideoReceiver *video_receiver);

/**
 * Called after receiving the Stream Info Packet.
 *
 * @param video_receiver
 * @param profiles Array of profiles. Ownership of the contained header buffers will be transferred to the LabsVideoReceiver!
 * @param profiles_count must be <= LABS_VIDEO_PROFILES_MAX
 */
LABS_EXPORT void labs_video_receiver_stream_info(LabsVideoReceiver *video_receiver, LabsVideoProfile *profiles, size_t profiles_count);

LABS_EXPORT void labs_video_receiver_av_packet(LabsVideoReceiver *video_receiver, LabsTakionAVPacket *packet);
LABS_EXPORT void labs_video_receiver_set_waiting_for_idr(LabsVideoReceiver *video_receiver, bool waiting_for_idr);
LABS_EXPORT bool labs_video_receiver_get_waiting_for_idr(LabsVideoReceiver *video_receiver);
LABS_EXPORT int32_t labs_video_receiver_get_frames_lost_total(LabsVideoReceiver *video_receiver);

static inline LabsVideoReceiver *labs_video_receiver_new(struct labs_session_t *session, LabsPacketStats *packet_stats)
{
	LabsVideoReceiver *video_receiver = LABS_NEW(LabsVideoReceiver);
	if(!video_receiver)
		return NULL;
	labs_video_receiver_init(video_receiver, session, packet_stats);
	return video_receiver;
}

static inline void labs_video_receiver_free(LabsVideoReceiver *video_receiver)
{
	if(!video_receiver)
		return;
	labs_video_receiver_fini(video_receiver);
	free(video_receiver);
}

#ifdef __cplusplus
}
#endif

#endif // LABS_VIDEORECEIVER_H
