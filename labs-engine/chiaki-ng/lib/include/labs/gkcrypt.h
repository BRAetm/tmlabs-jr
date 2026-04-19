// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_GKCRYPT_H
#define LABS_GKCRYPT_H

#include "common.h"
#include "log.h"
#include "thread.h"

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LABS_GKCRYPT_BLOCK_SIZE 0x10
#define LABS_GKCRYPT_KEY_BUF_BLOCKS_DEFAULT 0x40 // 256KB
#define LABS_GKCRYPT_GMAC_SIZE 4
#define LABS_GKCRYPT_GMAC_KEY_REFRESH_KEY_POS 45000
#define LABS_GKCRYPT_GMAC_KEY_REFRESH_IV_OFFSET 44910

typedef struct labs_key_state_t
{
   uint64_t prev;
} LabsKeyState;

typedef struct labs_gkcrypt_t {
	uint8_t index;

	uint8_t *key_buf; // circular buffer of the ctr mode key stream
	uint64_t key_buf_size;
	uint64_t key_buf_populated; // size of key_buf that is already populated (on startup)
	uint64_t key_buf_key_pos_min; // minimal key pos currently in key_buf
	size_t key_buf_start_offset; // offset in key_buf of the minimal key pos
	uint64_t last_key_pos;        // last key pos that has been requested
	bool key_buf_thread_stop;
	LabsMutex key_buf_mutex;
	LabsCond key_buf_cond;
	LabsThread key_buf_thread;

	uint8_t iv[LABS_GKCRYPT_BLOCK_SIZE];
	uint8_t key_base[LABS_GKCRYPT_BLOCK_SIZE];
	uint8_t key_gmac_base[LABS_GKCRYPT_BLOCK_SIZE];
	uint8_t key_gmac_current[LABS_GKCRYPT_BLOCK_SIZE];
	uint64_t key_gmac_index_current;
	LabsLog *log;
} LabsGKCrypt;

struct labs_session_t;

/**
 * @param key_buf_chunks if > 0, use a thread to generate the ctr mode key stream
 */
LABS_EXPORT LabsErrorCode labs_gkcrypt_init(LabsGKCrypt *gkcrypt, LabsLog *log, size_t key_buf_chunks, uint8_t index, const uint8_t *handshake_key, const uint8_t *ecdh_secret);

LABS_EXPORT void labs_gkcrypt_fini(LabsGKCrypt *gkcrypt);
LABS_EXPORT LabsErrorCode labs_gkcrypt_gen_key_stream(LabsGKCrypt *gkcrypt, uint64_t key_pos, uint8_t *buf, size_t buf_size);
LABS_EXPORT LabsErrorCode labs_gkcrypt_get_key_stream(LabsGKCrypt *gkcrypt, uint64_t key_pos, uint8_t *buf, size_t buf_size);
LABS_EXPORT LabsErrorCode labs_gkcrypt_decrypt(LabsGKCrypt *gkcrypt, uint64_t key_pos, uint8_t *buf, size_t buf_size);
static inline LabsErrorCode labs_gkcrypt_encrypt(LabsGKCrypt *gkcrypt, uint64_t key_pos, uint8_t *buf, size_t buf_size) { return labs_gkcrypt_decrypt(gkcrypt, key_pos, buf, buf_size); }
LABS_EXPORT void labs_gkcrypt_gen_gmac_key(uint64_t index, const uint8_t *key_base, const uint8_t *iv, uint8_t *key_out);
LABS_EXPORT void labs_gkcrypt_gen_new_gmac_key(LabsGKCrypt *gkcrypt, uint64_t index);
LABS_EXPORT void labs_gkcrypt_gen_tmp_gmac_key(LabsGKCrypt *gkcrypt, uint64_t index, uint8_t *key_out);
LABS_EXPORT LabsErrorCode labs_gkcrypt_gmac(LabsGKCrypt *gkcrypt, uint64_t key_pos, const uint8_t *buf, size_t buf_size, uint8_t *gmac_out);

static inline LabsGKCrypt *labs_gkcrypt_new(LabsLog *log, size_t key_buf_chunks, uint8_t index, const uint8_t *handshake_key, const uint8_t *ecdh_secret)
{
	LabsGKCrypt *gkcrypt = LABS_NEW(LabsGKCrypt);
	if(!gkcrypt)
		return NULL;
	LabsErrorCode err = labs_gkcrypt_init(gkcrypt, log, key_buf_chunks, index, handshake_key, ecdh_secret);
	if(err != LABS_ERR_SUCCESS)
	{
		free(gkcrypt);
		return NULL;
	}
	return gkcrypt;
}

static inline void labs_gkcrypt_free(LabsGKCrypt *gkcrypt)
{
	if(!gkcrypt)
		return;
	labs_gkcrypt_fini(gkcrypt);
	free(gkcrypt);
}

LABS_EXPORT void labs_key_state_init(LabsKeyState *state);

/**
 * @param commit whether to remember this key_pos to update the state. Should only be true after authentication to avoid DoS.
 */
LABS_EXPORT uint64_t labs_key_state_request_pos(LabsKeyState *state, uint32_t low, bool commit);

/**
 * Update the internal state after knowing that this key_pos is authentic.
 */
LABS_EXPORT void labs_key_state_commit(LabsKeyState *state, uint64_t prev);

#ifdef __cplusplus
}
#endif

#endif //LABS_GKCRYPT_H
