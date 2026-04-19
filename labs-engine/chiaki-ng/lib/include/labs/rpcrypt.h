// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_RPCRYPT_H
#define LABS_RPCRYPT_H

#include "common.h"

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LABS_RPCRYPT_KEY_SIZE 0x10

typedef struct labs_rpcrypt_t
{
	LabsTarget target;
	uint8_t bright[LABS_RPCRYPT_KEY_SIZE];
	uint8_t ambassador[LABS_RPCRYPT_KEY_SIZE];
} LabsRPCrypt;

LABS_EXPORT void labs_rpcrypt_bright_ambassador(LabsTarget target, uint8_t *bright, uint8_t *ambassador, const uint8_t *nonce, const uint8_t *morning);
LABS_EXPORT void labs_rpcrypt_aeropause_ps4_pre10(uint8_t *aeropause, const uint8_t *ambassador);
LABS_EXPORT LabsErrorCode labs_rpcrypt_aeropause(LabsTarget target, size_t key_1_off, uint8_t *aeropause, const uint8_t *ambassador);
LABS_EXPORT LabsErrorCode labs_rpcrypt_aeropause_psn(LabsTarget target, size_t key_1_off, uint8_t *aeropause, const uint8_t *ambassador);
LABS_EXPORT LabsErrorCode labs_rpcrypt_ambassador_from_aeropause(LabsTarget target, size_t key_1_off, const uint8_t *aeropause, uint8_t *ambassador);

LABS_EXPORT void labs_rpcrypt_init_auth(LabsRPCrypt *rpcrypt, LabsTarget target, const uint8_t *nonce, const uint8_t *morning);
LABS_EXPORT void labs_rpcrypt_init_regist_ps4_pre10(LabsRPCrypt *rpcrypt, const uint8_t *ambassador, uint32_t pin);
LABS_EXPORT LabsErrorCode labs_rpcrypt_init_regist(LabsRPCrypt *rpcrypt, LabsTarget target, const uint8_t *ambassador, size_t key_0_off, uint32_t pin);
LABS_EXPORT LabsErrorCode labs_rpcrypt_init_regist_psn(LabsRPCrypt *rpcrypt, LabsTarget target, const uint8_t *ambassador, size_t key_0_off, uint8_t *custom_data1, uint8_t *data1, uint8_t *data2);
LABS_EXPORT LabsErrorCode labs_rpcrypt_generate_iv(LabsRPCrypt *rpcrypt, uint8_t *iv, uint64_t counter);
LABS_EXPORT LabsErrorCode labs_rpcrypt_encrypt(LabsRPCrypt *rpcrypt, uint64_t counter, const uint8_t *in, uint8_t *out, size_t sz);
LABS_EXPORT LabsErrorCode labs_rpcrypt_decrypt(LabsRPCrypt *rpcrypt, uint64_t counter, const uint8_t *in, uint8_t *out, size_t sz);

#ifdef __cplusplus
}
#endif

#endif // LABS_RPCRYPT_H
