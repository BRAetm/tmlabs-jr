// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_OPUSENCODER_H
#define LABS_OPUSENCODER_H

#include <labs/config.h>
#if LABS_LIB_ENABLE_OPUS

#include "audiosender.h"
#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_opus_encoder_t
{
	LabsLog *log;
	struct OpusEncoder *opus_encoder;
	LabsAudioHeader audio_header;
	uint8_t *opus_frame_buf;
	size_t opus_frame_buf_size;
	LabsAudioSender *audio_sender;
} LabsOpusEncoder;

LABS_EXPORT void labs_opus_encoder_init(LabsOpusEncoder *encoder, LabsLog *log);
LABS_EXPORT void labs_opus_encoder_fini(LabsOpusEncoder *encoder);
LABS_EXPORT void labs_opus_encoder_header(LabsAudioHeader *header, LabsOpusEncoder *encoder, LabsSession *session);
LABS_EXPORT void labs_opus_encoder_frame(int16_t *pcm_buf, LabsOpusEncoder *header);

#ifdef __cplusplus
}
#endif

#endif

#endif // LABS_OPUSENCODER_H
