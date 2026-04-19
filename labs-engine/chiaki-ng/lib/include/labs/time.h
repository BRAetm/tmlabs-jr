// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_TIME_H
#define LABS_TIME_H

#include "common.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

LABS_EXPORT uint64_t labs_time_now_monotonic_us();

static inline uint64_t labs_time_now_monotonic_ms() { return labs_time_now_monotonic_us() / 1000; }

#ifdef __cplusplus
}
#endif

#endif // LABS_TIME_H
