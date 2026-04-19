// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_VIDEO_H
#define LABS_VIDEO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_video_profile_t
{
	unsigned int width;
	unsigned int height;
	size_t header_sz;
	uint8_t *header;
} LabsVideoProfile;

/**
 * Padding for FFMPEG
 */
#define LABS_VIDEO_BUFFER_PADDING_SIZE 64

#ifdef __cplusplus
}
#endif

#endif // LABS_VIDEO_H
