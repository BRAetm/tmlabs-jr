// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_AUDIO_H
#define LABS_AUDIO_H

#include <stdint.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LABS_AUDIO_HEADER_SIZE 0xe

typedef struct labs_audio_header_t
{
	uint8_t channels;
	uint8_t bits;
	uint32_t rate;
	uint32_t frame_size;
	uint32_t unknown;
} LabsAudioHeader;

LABS_EXPORT void labs_audio_header_set(LabsAudioHeader *audio_header, uint8_t channels, uint8_t bits, uint32_t rate, uint32_t frame_size);
LABS_EXPORT void labs_audio_header_load(LabsAudioHeader *audio_header, const uint8_t *buf);
LABS_EXPORT void labs_audio_header_save(LabsAudioHeader *audio_header, uint8_t *buf);

static inline size_t labs_audio_header_frame_buf_size(LabsAudioHeader *audio_header)
{
	return audio_header->frame_size * audio_header->channels * sizeof(int16_t);
}

#ifdef __cplusplus
}
#endif

#endif // LABS_AUDIO_H
