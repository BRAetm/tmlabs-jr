// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_OPUSDECODER_H
#define LABS_OPUSDECODER_H

#include <labs/config.h>
#if LABS_LIB_ENABLE_OPUS

#include "audioreceiver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*LabsOpusDecoderSettingsCallback)(uint32_t channels, uint32_t rate, void *user);
typedef void (*LabsOpusDecoderFrameCallback)(int16_t *buf, size_t samples_count, void *user);

typedef struct labs_opus_decoder_t
{
	LabsLog *log;
	struct OpusDecoder *opus_decoder;
	LabsAudioHeader audio_header;
	int16_t *pcm_buf;
	size_t pcm_buf_size;

	LabsOpusDecoderSettingsCallback settings_cb;
	LabsOpusDecoderFrameCallback frame_cb;
	void *cb_user;
} LabsOpusDecoder;

LABS_EXPORT void labs_opus_decoder_init(LabsOpusDecoder *decoder, LabsLog *log);
LABS_EXPORT void labs_opus_decoder_fini(LabsOpusDecoder *decoder);
LABS_EXPORT void labs_opus_decoder_get_sink(LabsOpusDecoder *decoder, LabsAudioSink *sink);

static inline void labs_opus_decoder_set_cb(LabsOpusDecoder *decoder, LabsOpusDecoderSettingsCallback settings_cb, LabsOpusDecoderFrameCallback frame_cb, void *user)
{
	decoder->settings_cb = settings_cb;
	decoder->frame_cb = frame_cb;
	decoder->cb_user = user;
}

#ifdef __cplusplus
}
#endif

#endif

#endif // LABS_OPUSDECODER_H
