// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_COMMON_H
#define LABS_COMMON_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __GNUC__
typedef uint32_t labs_unaligned_uint32_t __attribute__((aligned(1)));
typedef uint16_t labs_unaligned_uint16_t __attribute__((aligned(1)));
typedef int16_t labs_unaligned_int16_t __attribute__((aligned(1)));
#else
typedef uint32_t labs_unaligned_uint32_t;
typedef uint16_t labs_unaligned_uint16_t;
typedef int16_t labs_unaligned_int16_t;
#endif

#define LABS_EXPORT

#ifdef _WIN32
#define LABS_SSIZET_TYPE int
#else
#define LABS_SSIZET_TYPE ssize_t
#endif

#define LABS_NEW(t) ((t*)malloc(sizeof(t)))

typedef enum
{
	LABS_ERR_SUCCESS = 0,
	LABS_ERR_UNKNOWN,
	LABS_ERR_PARSE_ADDR,
	LABS_ERR_THREAD,
	LABS_ERR_MEMORY,
	LABS_ERR_OVERFLOW,
	LABS_ERR_NETWORK,
	LABS_ERR_CONNECTION_REFUSED,
	LABS_ERR_HOST_DOWN,
	LABS_ERR_HOST_UNREACH,
	LABS_ERR_DISCONNECTED,
	LABS_ERR_INVALID_DATA,
	LABS_ERR_BUF_TOO_SMALL,
	LABS_ERR_MUTEX_LOCKED,
	LABS_ERR_CANCELED,
	LABS_ERR_TIMEOUT,
	LABS_ERR_INVALID_RESPONSE,
	LABS_ERR_INVALID_MAC,
	LABS_ERR_UNINITIALIZED,
	LABS_ERR_FEC_FAILED,
	LABS_ERR_VERSION_MISMATCH,
	LABS_ERR_HTTP_NONOK
} LabsErrorCode;

LABS_EXPORT const char *labs_error_string(LabsErrorCode code);

LABS_EXPORT void *labs_aligned_alloc(size_t alignment, size_t size);
LABS_EXPORT void labs_aligned_free(void *ptr);

typedef enum
{
	// values must not change
	LABS_TARGET_PS4_UNKNOWN =       0,
	LABS_TARGET_PS4_8 =           800,
	LABS_TARGET_PS4_9 =           900,
	LABS_TARGET_PS4_10 =         1000,
	LABS_TARGET_PS5_UNKNOWN = 1000000,
	LABS_TARGET_PS5_1 =       1000100
} LabsTarget;

static inline bool labs_target_is_unknown(LabsTarget target)
{
	return target == LABS_TARGET_PS5_UNKNOWN
		|| target == LABS_TARGET_PS4_UNKNOWN;
}

static inline bool labs_target_is_ps5(LabsTarget target) { return target >= LABS_TARGET_PS5_UNKNOWN; }

/**
 * Perform initialization of global state needed for using the Labs lib
 */
LABS_EXPORT LabsErrorCode labs_lib_init();

typedef enum
{
	// values must not change
	LABS_CODEC_H264 = 0,
	LABS_CODEC_H265 = 1,
	LABS_CODEC_H265_HDR = 2
} LabsCodec;

static inline bool labs_codec_is_h265(LabsCodec codec)
{
	return codec == LABS_CODEC_H265 || codec == LABS_CODEC_H265_HDR;
}

static inline bool labs_codec_is_hdr(LabsCodec codec)
{
	return codec == LABS_CODEC_H265_HDR;
}

LABS_EXPORT const char *labs_codec_name(LabsCodec codec);

#ifdef __cplusplus
}
#endif

#endif // LABS_COMMON_H
