// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_AUDIORECEIVER_H
#define LABS_AUDIORECEIVER_H

#include "common.h"
#include "log.h"
#include "audio.h"
#include "takion.h"
#include "thread.h"
#include "packetstats.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*LabsAudioSinkHeader)(LabsAudioHeader *header, void *user);
typedef void (*LabsAudioSinkFrame)(uint8_t *buf, size_t buf_size, void *user);

/**
 * Sink that receives Audio encoded as Opus
 */
typedef struct labs_audio_sink_t
{
	void *user;
	LabsAudioSinkHeader header_cb;
	LabsAudioSinkFrame frame_cb;
} LabsAudioSink;

typedef struct labs_audio_receiver_t
{
	struct labs_session_t *session;
	LabsLog *log;
	LabsMutex mutex;
	LabsSeqNum16 frame_index_prev;
	LabsSeqNum16 next_frame_index;
	bool next_frame_index_valid;
	bool playback_started;
	bool frame_index_startup; // whether frame_index_prev has definitely not wrapped yet
	LabsPacketStats *packet_stats;
	struct {
		bool occupied;
		LabsSeqNum16 frame_index;
		uint8_t *buf;
		size_t buf_size;
	} jitter_buffer[8];
	size_t jitter_buffer_count;
} LabsAudioReceiver;

LABS_EXPORT LabsErrorCode labs_audio_receiver_init(LabsAudioReceiver *audio_receiver, struct labs_session_t *session, LabsPacketStats *packet_stats);
LABS_EXPORT void labs_audio_receiver_fini(LabsAudioReceiver *audio_receiver);
LABS_EXPORT void labs_audio_receiver_stream_info(LabsAudioReceiver *audio_receiver, LabsAudioHeader *audio_header);
LABS_EXPORT void labs_audio_receiver_av_packet(LabsAudioReceiver *audio_receiver, LabsTakionAVPacket *packet);

static inline LabsAudioReceiver *labs_audio_receiver_new(struct labs_session_t *session, LabsPacketStats *packet_stats)
{
	LabsAudioReceiver *audio_receiver = LABS_NEW(LabsAudioReceiver);
	if(!audio_receiver)
		return NULL;
	LabsErrorCode err = labs_audio_receiver_init(audio_receiver, session, packet_stats);
	if(err != LABS_ERR_SUCCESS)
	{
		free(audio_receiver);
		return NULL;
	}
	return audio_receiver;
}

static inline void labs_audio_receiver_free(LabsAudioReceiver *audio_receiver)
{
	if(!audio_receiver)
		return;
	labs_audio_receiver_fini(audio_receiver);
	free(audio_receiver);
}

#ifdef __cplusplus
}
#endif

#endif // LABS_AUDIORECEIVER_H
