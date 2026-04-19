// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/common.h>
#include <labs/fec.h>
#include <labs/random.h>

#include <galois.h>

#include <errno.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#endif

LABS_EXPORT const char *labs_error_string(LabsErrorCode code)
{
	switch(code)
	{
		case LABS_ERR_SUCCESS:
			return "Success";
		case LABS_ERR_PARSE_ADDR:
			return "Failed to parse host address";
		case LABS_ERR_THREAD:
			return "Thread error";
		case LABS_ERR_MEMORY:
			return "Memory error";
		case LABS_ERR_NETWORK:
			return "Network error";
		case LABS_ERR_CONNECTION_REFUSED:
			return "Connection Refused";
		case LABS_ERR_HOST_DOWN:
			return "Host is down";
		case LABS_ERR_HOST_UNREACH:
			return "No route to host";
		case LABS_ERR_DISCONNECTED:
			return "Disconnected";
		case LABS_ERR_INVALID_DATA:
			return "Invalid data";
		case LABS_ERR_BUF_TOO_SMALL:
			return "Buffer too small";
		case LABS_ERR_MUTEX_LOCKED:
			return "Mutex is locked";
		case LABS_ERR_CANCELED:
			return "Canceled";
		case LABS_ERR_TIMEOUT:
			return "Timeout";
		case LABS_ERR_INVALID_RESPONSE:
			return "Invalid Response";
		case LABS_ERR_INVALID_MAC:
			return "Invalid MAC";
		case LABS_ERR_UNINITIALIZED:
			return "Uninitialized";
		case LABS_ERR_FEC_FAILED:
			return "FEC failed";
		default:
			return "Unknown";
	}
}

LABS_EXPORT void *labs_aligned_alloc(size_t alignment, size_t size)
{
#if defined(_WIN32)
	return _aligned_malloc(size, alignment);
#elif __APPLE__ || __ANDROID__
	void *r;
	if(posix_memalign(&r, alignment, size) == 0)
		return r;
	else
		return NULL;
#else
	return aligned_alloc(alignment, size);
#endif
}

LABS_EXPORT void labs_aligned_free(void *ptr)
{
#ifdef _WIN32
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

LABS_EXPORT LabsErrorCode labs_lib_init()
{
	unsigned int seed;
	labs_random_bytes_crypt((uint8_t *)&seed, sizeof(seed));
	srand(seed); // doesn't necessarily need to be secure for crypto

	int galois_r = galois_init_default_field(LABS_FEC_WORDSIZE);
	if(galois_r != 0)
		return galois_r == ENOMEM ? LABS_ERR_MEMORY : LABS_ERR_UNKNOWN;

#if _WIN32
	{
		WORD wsa_version = MAKEWORD(2, 2);
		WSADATA wsa_data;
		int err = WSAStartup(wsa_version, &wsa_data);
		if(err != 0)
			return LABS_ERR_NETWORK;
	}
#endif

	return LABS_ERR_SUCCESS;
}

LABS_EXPORT const char *labs_codec_name(LabsCodec codec)
{
	switch(codec)
	{
		case LABS_CODEC_H264:
			return "H264";
		case LABS_CODEC_H265:
			return "H265";
		case LABS_CODEC_H265_HDR:
			return "H265/HDR";
		default:
			return "unknown";
	}
}
