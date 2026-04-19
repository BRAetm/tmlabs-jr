// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_BASE64_H
#define LABS_BASE64_H

#include "common.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

LABS_EXPORT LabsErrorCode labs_base64_encode(const uint8_t *in, size_t in_size, char *out, size_t out_size);
LABS_EXPORT LabsErrorCode labs_base64_decode(const char *in, size_t in_size, uint8_t *out, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif // LABS_BASE64_H
