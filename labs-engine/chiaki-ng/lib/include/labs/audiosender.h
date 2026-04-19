// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_AUDIOSENDER_H
#define LABS_AUDIOSENDER_H

#include "common.h"
#include "log.h"
#include "audio.h"
#include "takion.h"
#include "thread.h"
#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct labs_audio_sender_t
{
	LabsLog *log;
	LabsMutex mutex;
	bool ps5;
	LabsTakion *takion;
	uint16_t buf_size_per_unit;
	uint16_t buf_stride_per_unit;
	uint8_t *frame_buf;
	uint8_t *framea;
	uint8_t *frameb;
	size_t frame_buf_size;
	uint8_t *filled_packet_buf;
	LabsSeqNum16 frame_index;
} LabsAudioSender;

LABS_EXPORT LabsErrorCode labs_audio_sender_init(LabsAudioSender *audio_sender, LabsLog *log, LabsSession *session);
LABS_EXPORT void labs_audio_sender_fini(LabsAudioSender *audio_sender);
LABS_EXPORT void labs_audio_sender_opus_data(LabsAudioSender *audio_sender, uint8_t *opus_data, size_t opus_data_size);

static inline LabsAudioSender *labs_audio_sender_new(LabsLog *log, LabsSession *session)
{
	LabsAudioSender *audio_sender = LABS_NEW(LabsAudioSender);
	if(!audio_sender)
		return NULL;
	LabsErrorCode err = labs_audio_sender_init(audio_sender, log, session);
	if(err != LABS_ERR_SUCCESS)
	{
		free(audio_sender);
		return NULL;
	}
	return audio_sender;
}

static inline void labs_audio_sender_free(LabsAudioSender *audio_sender)
{
	if(!audio_sender)
		return;
	labs_audio_sender_fini(audio_sender);
	free(audio_sender);
}

#ifdef __cplusplus
}
#endif

#endif // LABS_AUDIOSENDER_H
