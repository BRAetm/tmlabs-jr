// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/gkcrypt.h>
#include <labs/session.h>

#include <string.h>
#include <assert.h>

#ifdef LABS_LIB_ENABLE_MBEDTLS
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/gcm.h"
#include "mbedtls/sha256.h"
#else
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#endif

#include "utils.h"

#define KEY_BUF_CHUNK_SIZE 0x1000

static LabsErrorCode gkcrypt_gen_key_iv(LabsGKCrypt *gkcrypt, uint8_t index, const uint8_t *handshake_key, const uint8_t *ecdh_secret);

static void *gkcrypt_thread_func(void *user);

LABS_EXPORT LabsErrorCode labs_gkcrypt_init(LabsGKCrypt *gkcrypt, LabsLog *log, size_t key_buf_chunks, uint8_t index, const uint8_t *handshake_key, const uint8_t *ecdh_secret)
{
	gkcrypt->log = log;
	gkcrypt->index = index;

	gkcrypt->key_buf_size = key_buf_chunks * KEY_BUF_CHUNK_SIZE;
	gkcrypt->key_buf_populated = 0;
	gkcrypt->key_buf_key_pos_min = 0;
	gkcrypt->key_buf_start_offset = 0;
	gkcrypt->last_key_pos = 0;
	gkcrypt->key_buf_thread_stop = false;

	LabsErrorCode err;
	if(gkcrypt->key_buf_size)
	{
		gkcrypt->key_buf = labs_aligned_alloc(KEY_BUF_CHUNK_SIZE, gkcrypt->key_buf_size);
		if(!gkcrypt->key_buf)
		{
			err = LABS_ERR_MEMORY;
			goto error;
		}

		err = labs_mutex_init(&gkcrypt->key_buf_mutex, false);
		if(err != LABS_ERR_SUCCESS)
			goto error_key_buf;

		err = labs_cond_init(&gkcrypt->key_buf_cond);
		if(err != LABS_ERR_SUCCESS)
			goto error_key_buf_mutex;
	}
	else
	{
		gkcrypt->key_buf = NULL;
	}
	err = gkcrypt_gen_key_iv(gkcrypt, index, handshake_key, ecdh_secret);
	if(err != LABS_ERR_SUCCESS)
	{
		LABS_LOGE(gkcrypt->log, "GKCrypt failed to generate key and IV");
		goto error_key_buf_cond;
	}

	labs_gkcrypt_gen_gmac_key(0, gkcrypt->key_base, gkcrypt->iv, gkcrypt->key_gmac_base);
	gkcrypt->key_gmac_index_current = 0;
	memcpy(gkcrypt->key_gmac_current, gkcrypt->key_gmac_base, sizeof(gkcrypt->key_gmac_current));

	if(gkcrypt->key_buf)
	{
		err = labs_thread_create(&gkcrypt->key_buf_thread, gkcrypt_thread_func, gkcrypt);
		if(err != LABS_ERR_SUCCESS)
			goto error_key_buf_cond;

		labs_thread_set_name(&gkcrypt->key_buf_thread, "Labs GKCrypt");
	}

	return LABS_ERR_SUCCESS;

error_key_buf_cond:
	if(gkcrypt->key_buf)
		labs_cond_fini(&gkcrypt->key_buf_cond);
error_key_buf_mutex:
	if(gkcrypt->key_buf)
		labs_mutex_fini(&gkcrypt->key_buf_mutex);
error_key_buf:
	labs_aligned_free(gkcrypt->key_buf);
error:
	return err;
}

LABS_EXPORT void labs_gkcrypt_fini(LabsGKCrypt *gkcrypt)
{
	if(gkcrypt->key_buf)
	{
		labs_mutex_lock(&gkcrypt->key_buf_mutex);
		gkcrypt->key_buf_thread_stop = true;
		labs_mutex_unlock(&gkcrypt->key_buf_mutex);
		labs_cond_signal(&gkcrypt->key_buf_cond);
		labs_thread_join(&gkcrypt->key_buf_thread, NULL);
		labs_cond_fini(&gkcrypt->key_buf_cond);
		labs_mutex_fini(&gkcrypt->key_buf_mutex);
		labs_aligned_free(gkcrypt->key_buf);
	}
}

static LabsErrorCode gkcrypt_gen_key_iv(LabsGKCrypt *gkcrypt, uint8_t index, const uint8_t *handshake_key, const uint8_t *ecdh_secret)
{
	uint8_t data[3 + LABS_HANDSHAKE_KEY_SIZE + 2];
	data[0] = 1;
	data[1] = index;
	data[2] = 0;
	memcpy(data + 3, handshake_key, LABS_HANDSHAKE_KEY_SIZE);
	data[3 + LABS_HANDSHAKE_KEY_SIZE + 0] = 1;
	data[3 + LABS_HANDSHAKE_KEY_SIZE + 1] = 0;

	uint8_t hmac[LABS_GKCRYPT_BLOCK_SIZE*2];
	size_t hmac_size = sizeof(hmac);
#ifdef LABS_LIB_ENABLE_MBEDTLS
	mbedtls_md_context_t ctx;
	mbedtls_md_init(&ctx);

	if(mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1) != 0)
	{
		mbedtls_md_free(&ctx);
		return LABS_ERR_UNKNOWN;
	}

	if(mbedtls_md_hmac_starts(&ctx, ecdh_secret, LABS_ECDH_SECRET_SIZE) != 0)
	{
		mbedtls_md_free(&ctx);
		return LABS_ERR_UNKNOWN;
	}

	if(mbedtls_md_hmac_update(&ctx, data, sizeof(data)) != 0)
	{
		mbedtls_md_free(&ctx);
		return LABS_ERR_UNKNOWN;
	}

	if(mbedtls_md_hmac_finish(&ctx, hmac) != 0)
	{
		mbedtls_md_free(&ctx);
		return LABS_ERR_UNKNOWN;
	}

	mbedtls_md_free(&ctx);

#else
	if(!HMAC(EVP_sha256(), ecdh_secret, LABS_ECDH_SECRET_SIZE, data, sizeof(data), hmac, (unsigned int *)&hmac_size))
		return LABS_ERR_UNKNOWN;

#endif
	assert(hmac_size == sizeof(hmac));

	memcpy(gkcrypt->key_base, hmac, LABS_GKCRYPT_BLOCK_SIZE);
	memcpy(gkcrypt->iv, hmac + LABS_GKCRYPT_BLOCK_SIZE, LABS_GKCRYPT_BLOCK_SIZE);

	return LABS_ERR_SUCCESS;
}

static inline void counter_add(uint8_t *out, const uint8_t *base, uint64_t v)
{
	size_t i=0;
	do
	{
		uint64_t r = base[i] + v;
		out[i] = (uint8_t)(r & 0xff);
		v = r >> 8;
		i++;
	} while(i<LABS_GKCRYPT_BLOCK_SIZE && v);

	if(i < LABS_GKCRYPT_BLOCK_SIZE)
		memcpy(out + i, base + i, LABS_GKCRYPT_BLOCK_SIZE - i);
}

LABS_EXPORT void labs_gkcrypt_gen_gmac_key(uint64_t index, const uint8_t *key_base, const uint8_t *iv, uint8_t *key_out)
{
	uint8_t data[0x20];
	memcpy(data, key_base, 0x10);
	counter_add(data + 0x10, iv, index * LABS_GKCRYPT_GMAC_KEY_REFRESH_IV_OFFSET);
	uint8_t md[0x20];
#ifdef LABS_LIB_ENABLE_MBEDTLS
	// last param
	// is224 Determines which function to use.
	// This must be either 0 for SHA-256, or 1 for SHA-224.
	
	//https://github.com/Mbed-TLS/mbedtls/blob/0cc68601e6f602ae909fd701217a885396cdcee9/docs/3.0-migration-guide.md?plain=1#L346
	// mbedtls_sha256_ret -> mbedtls_sha256
	mbedtls_sha256(data, sizeof(data), md, 0);
#else
	SHA256(data, 0x20, md);
#endif
	xor_bytes(md, md + 0x10, 0x10);
	memcpy(key_out, md, LABS_GKCRYPT_BLOCK_SIZE);
}

LABS_EXPORT void labs_gkcrypt_gen_new_gmac_key(LabsGKCrypt *gkcrypt, uint64_t index)
{
	assert(index > 0);
	labs_gkcrypt_gen_gmac_key(index, gkcrypt->key_gmac_base, gkcrypt->iv, gkcrypt->key_gmac_current);
	gkcrypt->key_gmac_index_current = index;
}

LABS_EXPORT void labs_gkcrypt_gen_tmp_gmac_key(LabsGKCrypt *gkcrypt, uint64_t index, uint8_t *key_out)
{
	if(index)
		labs_gkcrypt_gen_gmac_key(index, gkcrypt->key_gmac_base, gkcrypt->iv, key_out);
	else
		memcpy(key_out, gkcrypt->key_gmac_base, sizeof(gkcrypt->key_gmac_base));
}

LABS_EXPORT LabsErrorCode labs_gkcrypt_gen_key_stream(LabsGKCrypt *gkcrypt, uint64_t key_pos, uint8_t *buf, size_t buf_size)
{
	assert(key_pos % LABS_GKCRYPT_BLOCK_SIZE == 0);
	assert(buf_size % LABS_GKCRYPT_BLOCK_SIZE == 0);

#ifdef LABS_LIB_ENABLE_MBEDTLS
	// build mbedtls aes context
	mbedtls_aes_context ctx;
	mbedtls_aes_init(&ctx);

	if(mbedtls_aes_setkey_enc(&ctx, gkcrypt->key_base, 128) != 0)
	{
		mbedtls_aes_free(&ctx);
		return LABS_ERR_UNKNOWN;
	}

#else
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if(!ctx)
		return LABS_ERR_UNKNOWN;

	if(!EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, gkcrypt->key_base, NULL))
	{
		EVP_CIPHER_CTX_free(ctx);
		return LABS_ERR_UNKNOWN;
	}

	if(!EVP_CIPHER_CTX_set_padding(ctx, 0))
	{
		EVP_CIPHER_CTX_free(ctx);
		return LABS_ERR_UNKNOWN;
	}
#endif
	uint64_t counter_offset = (key_pos / LABS_GKCRYPT_BLOCK_SIZE);

	for(uint8_t *cur = buf, *end = buf + buf_size; cur < end; cur += LABS_GKCRYPT_BLOCK_SIZE)
		counter_add(cur, gkcrypt->iv, counter_offset++);

#ifdef LABS_LIB_ENABLE_MBEDTLS
	for(int i = 0; i < buf_size; i = i + 16)
	{
		// loop over all blocks of 16 bytes (128 bits)
		if(mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, buf + i, buf + i) != 0)
		{
			mbedtls_aes_free(&ctx);
			return LABS_ERR_UNKNOWN;
		}
	}

	mbedtls_aes_free(&ctx);
#else
	int outl;
	EVP_EncryptUpdate(ctx, buf, &outl, buf, (int)buf_size);
	if(outl != buf_size)
	{
		EVP_CIPHER_CTX_free(ctx);
		return LABS_ERR_UNKNOWN;
	}

	EVP_CIPHER_CTX_free(ctx);
#endif
	return LABS_ERR_SUCCESS;
}

static bool gkcrypt_key_buf_should_generate(LabsGKCrypt *gkcrypt)
{
	return gkcrypt->last_key_pos > gkcrypt->key_buf_key_pos_min + gkcrypt->key_buf_populated / 2;
}

LABS_EXPORT LabsErrorCode labs_gkcrypt_get_key_stream(LabsGKCrypt *gkcrypt, uint64_t key_pos, uint8_t *buf, size_t buf_size)
{
	if(!gkcrypt->key_buf)
		return labs_gkcrypt_gen_key_stream(gkcrypt, key_pos, buf, buf_size);

	labs_mutex_lock(&gkcrypt->key_buf_mutex);

	if(key_pos + buf_size > gkcrypt->last_key_pos)
		gkcrypt->last_key_pos = key_pos + buf_size;
	bool signal = gkcrypt_key_buf_should_generate(gkcrypt);

	LabsErrorCode err;
	if(key_pos < gkcrypt->key_buf_key_pos_min
		|| key_pos + buf_size >= gkcrypt->key_buf_key_pos_min + gkcrypt->key_buf_populated)
	{
		LABS_LOGW(gkcrypt->log, "Requested key stream for key pos %#llx on GKCrypt %d, but it's not in the buffer:"
				" key buf size %#llx, start offset: %#llx, populated: %#llx, min key pos: %#llx, last key pos: %#llx",
				(unsigned long long)key_pos,
				gkcrypt->index,
				(unsigned long long)gkcrypt->key_buf_size,
				(unsigned long long)gkcrypt->key_buf_start_offset,
				(unsigned long long)gkcrypt->key_buf_populated,
				(unsigned long long)gkcrypt->key_buf_key_pos_min,
				(unsigned long long)gkcrypt->last_key_pos);
		labs_mutex_unlock(&gkcrypt->key_buf_mutex);
		err = labs_gkcrypt_gen_key_stream(gkcrypt, key_pos, buf, buf_size);
	}
	else
	{
		size_t offset_in_buf = key_pos - gkcrypt->key_buf_key_pos_min + gkcrypt->key_buf_start_offset;
		offset_in_buf %= gkcrypt->key_buf_size;
		size_t end = offset_in_buf + buf_size;
		if(end > gkcrypt->key_buf_size)
		{
			size_t excess = end - gkcrypt->key_buf_size;
			memcpy(buf, gkcrypt->key_buf + offset_in_buf, buf_size - excess);
			memcpy(buf + (buf_size - excess), gkcrypt->key_buf, excess);
		}
		else
			memcpy(buf, gkcrypt->key_buf + offset_in_buf, buf_size);
		err = LABS_ERR_SUCCESS;
		labs_mutex_unlock(&gkcrypt->key_buf_mutex);
	}

	if(signal)
		labs_cond_signal(&gkcrypt->key_buf_cond);

	return err;
}

LABS_EXPORT LabsErrorCode labs_gkcrypt_decrypt(LabsGKCrypt *gkcrypt, uint64_t key_pos, uint8_t *buf, size_t buf_size)
{
	uint64_t padding_pre = key_pos % LABS_GKCRYPT_BLOCK_SIZE;
	size_t full_size = ((padding_pre + buf_size + LABS_GKCRYPT_BLOCK_SIZE - 1) / LABS_GKCRYPT_BLOCK_SIZE) * LABS_GKCRYPT_BLOCK_SIZE;

	uint8_t *key_stream = malloc(full_size);
	if(!key_stream)
		return LABS_ERR_MEMORY;

	LabsErrorCode err = labs_gkcrypt_get_key_stream(gkcrypt, key_pos - padding_pre, key_stream, full_size);
	if(err != LABS_ERR_SUCCESS)
	{
		free(key_stream);
		return err;
	}

	xor_bytes(buf, key_stream + padding_pre, buf_size);
	free(key_stream);

	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_gkcrypt_gmac(LabsGKCrypt *gkcrypt, uint64_t key_pos, const uint8_t *buf, size_t buf_size, uint8_t *gmac_out)
{
	uint8_t iv[LABS_GKCRYPT_BLOCK_SIZE];
	counter_add(iv, gkcrypt->iv, key_pos / 0x10);

	uint8_t *gmac_key = gkcrypt->key_gmac_current;
	uint8_t gmac_key_tmp[LABS_GKCRYPT_BLOCK_SIZE];
	uint64_t key_index = (key_pos > 0 ? key_pos - 1 : 0) / LABS_GKCRYPT_GMAC_KEY_REFRESH_KEY_POS;

	if(key_index > gkcrypt->key_gmac_index_current)
	{
		labs_gkcrypt_gen_new_gmac_key(gkcrypt, key_index);
	}
	else if(key_index < gkcrypt->key_gmac_index_current)
	{
		labs_gkcrypt_gen_tmp_gmac_key(gkcrypt, key_index, gmac_key_tmp);
		gmac_key = gmac_key_tmp;
	}

#ifdef LABS_LIB_ENABLE_MBEDTLS
	// build mbedtls gcm context AES_128_GCM
	// Encryption
	mbedtls_gcm_context actx;
	mbedtls_gcm_init(&actx);
	// set gmac_key 128 bits key
	if(mbedtls_gcm_setkey(&actx, MBEDTLS_CIPHER_ID_AES, gmac_key, LABS_GKCRYPT_BLOCK_SIZE * 8) != 0)
	{
		mbedtls_gcm_free(&actx);
		return LABS_ERR_UNKNOWN;
	}

	// encrypt without additional data
	if(mbedtls_gcm_starts(&actx, MBEDTLS_GCM_ENCRYPT, iv, LABS_GKCRYPT_BLOCK_SIZE, NULL, 0) != 0)
	{
		mbedtls_gcm_free(&actx);
		return LABS_ERR_UNKNOWN;
	}
	// set "additional data" only whitout input nor output
	// to get the same result as:
	// EVP_EncryptUpdate(ctx, NULL, &len, buf, (int)buf_size)
	if(mbedtls_gcm_crypt_and_tag(&actx, MBEDTLS_GCM_ENCRYPT,
		   0, iv, LABS_GKCRYPT_BLOCK_SIZE,
		   buf, buf_size, NULL, NULL,
		   LABS_GKCRYPT_GMAC_SIZE, gmac_out) != 0)
	{

		mbedtls_gcm_free(&actx);
		return LABS_ERR_UNKNOWN;
	}

	mbedtls_gcm_free(&actx);

	return LABS_ERR_SUCCESS;
#else
	LabsErrorCode ret = LABS_ERR_SUCCESS;

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if(!ctx)
	{
		ret = LABS_ERR_MEMORY;
		goto fail;
	}

	if(!EVP_CipherInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL, 1))
	{
		ret = LABS_ERR_UNKNOWN;
		goto fail_cipher;
	}

	if(!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, LABS_GKCRYPT_BLOCK_SIZE, NULL))
	{
		ret = LABS_ERR_UNKNOWN;
		goto fail_cipher;
	}

	if(!EVP_CipherInit_ex(ctx, NULL, NULL, gmac_key, iv, 1))
	{
		ret = LABS_ERR_UNKNOWN;
		goto fail_cipher;
	}

	int len;
	if(!EVP_EncryptUpdate(ctx, NULL, &len, buf, (int)buf_size))
	{
		ret = LABS_ERR_UNKNOWN;
		goto fail_cipher;
	}

	if(!EVP_EncryptFinal_ex(ctx, NULL, &len))
	{
		ret = LABS_ERR_UNKNOWN;
		goto fail_cipher;
	}

	if(!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, LABS_GKCRYPT_GMAC_SIZE, gmac_out))
	{
		ret = LABS_ERR_UNKNOWN;
		goto fail_cipher;
	}

fail_cipher:
	EVP_CIPHER_CTX_free(ctx);
fail:
	return ret;
#endif
}

static bool key_buf_mutex_pred(void *user)
{
	LabsGKCrypt *gkcrypt = user;
	if(gkcrypt->key_buf_thread_stop)
		return true;

	if(gkcrypt->key_buf_populated < gkcrypt->key_buf_size)
		return true;

	if(gkcrypt_key_buf_should_generate(gkcrypt))
		return true;

	return false;
}

static LabsErrorCode gkcrypt_generate_next_chunk(LabsGKCrypt *gkcrypt)
{
	assert(gkcrypt->key_buf_populated + KEY_BUF_CHUNK_SIZE <= gkcrypt->key_buf_size);
	size_t buf_offset = (gkcrypt->key_buf_start_offset + gkcrypt->key_buf_populated) % gkcrypt->key_buf_size;
	uint64_t key_pos = gkcrypt->key_buf_key_pos_min + gkcrypt->key_buf_populated;
	uint8_t *buf_start = gkcrypt->key_buf + buf_offset;

	labs_mutex_unlock(&gkcrypt->key_buf_mutex);

	LabsErrorCode err = labs_gkcrypt_gen_key_stream(gkcrypt, key_pos, buf_start, KEY_BUF_CHUNK_SIZE);
	if(err != LABS_ERR_SUCCESS)
		LABS_LOGE(gkcrypt->log, "GKCrypt failed to generate key stream chunk");

	labs_mutex_lock(&gkcrypt->key_buf_mutex);

	if(err == LABS_ERR_SUCCESS)
		gkcrypt->key_buf_populated += KEY_BUF_CHUNK_SIZE;

	return err;
}

static void *gkcrypt_thread_func(void *user)
{
	LabsGKCrypt *gkcrypt = user;
	labs_thread_set_affinity(LABS_THREAD_NAME_GKCRYPT);
	LABS_LOGV(gkcrypt->log, "GKCrypt %d thread starting", (int)gkcrypt->index);

	LabsErrorCode err = labs_mutex_lock(&gkcrypt->key_buf_mutex);
	assert(err == LABS_ERR_SUCCESS);
	while(1)
	{
		err = labs_cond_wait_pred(&gkcrypt->key_buf_cond, &gkcrypt->key_buf_mutex, key_buf_mutex_pred, gkcrypt);

		if(gkcrypt->key_buf_thread_stop || err != LABS_ERR_SUCCESS)
			break;

		/*
		LABS_LOGV(gkcrypt->log, "GKCrypt %d key buf size %#llx, start offset: %#llx, populated: %#llx, min key pos: %#llx, last key pos: %#llx, generating next chunk",
					(int)gkcrypt->index,
					(unsigned long long)gkcrypt->key_buf_size,
					(unsigned long long)gkcrypt->key_buf_start_offset,
					(unsigned long long)gkcrypt->key_buf_populated,
					(unsigned long long)gkcrypt->key_buf_key_pos_min,
					(unsigned long long)gkcrypt->last_key_pos);
		*/

		if(gkcrypt->last_key_pos > gkcrypt->key_buf_key_pos_min + gkcrypt->key_buf_populated)
		{
			// skip ahead if the last key pos is already beyond our buffer
			uint64_t key_pos = (gkcrypt->last_key_pos / KEY_BUF_CHUNK_SIZE) * KEY_BUF_CHUNK_SIZE;
			LABS_LOGW(gkcrypt->log, "Already requested a higher key pos than in the buffer, skipping ahead from min %#llx to %#llx",
						(unsigned long long)gkcrypt->key_buf_key_pos_min,
						(unsigned long long)key_pos);
			gkcrypt->key_buf_key_pos_min = key_pos;
			gkcrypt->key_buf_start_offset = 0;
			gkcrypt->key_buf_populated = 0;
		}
		else if(gkcrypt->key_buf_populated == gkcrypt->key_buf_size)
		{
			gkcrypt->key_buf_start_offset = (gkcrypt->key_buf_start_offset + KEY_BUF_CHUNK_SIZE) % gkcrypt->key_buf_size;
			gkcrypt->key_buf_key_pos_min += KEY_BUF_CHUNK_SIZE;
			gkcrypt->key_buf_populated -= KEY_BUF_CHUNK_SIZE;
		}
		err = gkcrypt_generate_next_chunk(gkcrypt);
		if(err != LABS_ERR_SUCCESS)
			break;
	}

	labs_mutex_unlock(&gkcrypt->key_buf_mutex);
	return NULL;
}

LABS_EXPORT void labs_key_state_init(LabsKeyState *state)
{
	state->prev = 0;
}

LABS_EXPORT uint64_t labs_key_state_request_pos(LabsKeyState *state, uint32_t low, bool commit)
{
	uint32_t prev_low = (uint32_t)state->prev;
	uint32_t high = (uint32_t)(state->prev >> 32);
	if(labs_seq_num_32_gt(low, prev_low) && low < prev_low)
		high++;
	else if(labs_seq_num_32_lt(low, prev_low) && low > prev_low && high)
		high--;
	uint64_t res = (((uint64_t)high) << 32) | ((uint64_t)low);
	if(commit)
		state->prev = res;
	return res;
}

LABS_EXPORT void labs_key_state_commit(LabsKeyState *state, uint64_t prev)
{
	state->prev = prev;
}
