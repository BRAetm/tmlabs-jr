// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_ECDH_H
#define LABS_ECDH_H

#include "common.h"

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef LABS_LIB_ENABLE_MBEDTLS
#include "mbedtls/ecdh.h"
#include "mbedtls/ctr_drbg.h"
#endif


#define LABS_ECDH_SECRET_SIZE 32

typedef struct labs_ecdh_t
{
// the following lines may lead to memory corruption
// LABS_LIB_ENABLE_MBEDTLS must be defined
// globally (whole project)
#ifdef LABS_LIB_ENABLE_MBEDTLS
	// mbedtls ecdh context
	mbedtls_ecdh_context ctx;
	// deterministic random bit generator
	mbedtls_ctr_drbg_context drbg;
#else
	struct ec_group_st *group;
	struct ec_key_st *key_local;
#endif
} LabsECDH;

LABS_EXPORT LabsErrorCode labs_ecdh_init(LabsECDH *ecdh);
LABS_EXPORT void labs_ecdh_fini(LabsECDH *ecdh);
LABS_EXPORT LabsErrorCode labs_ecdh_get_local_pub_key(LabsECDH *ecdh, uint8_t *key_out, size_t *key_out_size, const uint8_t *handshake_key, uint8_t *sig_out, size_t *sig_out_size);
LABS_EXPORT LabsErrorCode labs_ecdh_derive_secret(LabsECDH *ecdh, uint8_t *secret_out, const uint8_t *remote_key, size_t remote_key_size, const uint8_t *handshake_key, const uint8_t *remote_sig, size_t remote_sig_size);
LABS_EXPORT LabsErrorCode labs_ecdh_set_local_key(LabsECDH *ecdh, const uint8_t *private_key, size_t private_key_size, const uint8_t *public_key, size_t public_key_size);

#ifdef __cplusplus
}
#endif

#endif // LABS_ECDH_H
