// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_LAUNCHSPEC_H
#define LABS_LAUNCHSPEC_H

#include "common.h"

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_launch_spec_t
{
	LabsTarget target;
	unsigned int mtu;
	unsigned int rtt;
	uint8_t *handshake_key;
	unsigned int width;
	unsigned int height;
	unsigned int max_fps;
	LabsCodec codec;
	unsigned int bw_kbps_sent;
} LabsLaunchSpec;

LABS_EXPORT int labs_launchspec_format(char *buf, size_t buf_size, LabsLaunchSpec *launch_spec);

#ifdef __cplusplus
}
#endif

#endif // LABS_LAUNCHSPEC_H
