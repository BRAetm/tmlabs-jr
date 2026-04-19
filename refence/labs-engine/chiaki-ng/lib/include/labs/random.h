// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_RANDOM_H
#define LABS_RANDOM_H

#include "common.h"

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Random for cryptography
 */
LABS_EXPORT LabsErrorCode labs_random_bytes_crypt(uint8_t *buf, size_t buf_size);

LABS_EXPORT uint32_t labs_random_32();

#ifdef __cplusplus
}
#endif

#endif // LABS_RANDOM_H
