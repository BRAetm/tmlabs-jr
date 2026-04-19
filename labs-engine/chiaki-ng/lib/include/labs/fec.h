// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_FEC_H
#define LABS_FEC_H

#include "common.h"

#include <stdint.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LABS_FEC_WORDSIZE 8

LABS_EXPORT LabsErrorCode labs_fec_decode(uint8_t *frame_buf, size_t unit_size, size_t stride, unsigned int k, unsigned int m, const unsigned int *erasures, size_t erasures_count);
LABS_EXPORT LabsErrorCode labs_fec_encode(uint8_t *frame_buf, size_t unit_size, size_t stride, unsigned int k, unsigned int m);

#ifdef __cplusplus
}
#endif

#endif //LABS_FEC_H
