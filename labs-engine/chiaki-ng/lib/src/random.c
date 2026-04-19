// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/random.h>

#ifdef LABS_LIB_ENABLE_MBEDTLS
//#include <mbedtls/havege.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#else
#include <openssl/rand.h>
#endif

LABS_EXPORT LabsErrorCode labs_random_bytes_crypt(uint8_t *buf, size_t buf_size)
{
#ifdef LABS_LIB_ENABLE_MBEDTLS
	// mbedtls_havege_state hs;
	// mbedtls_havege_init(&hs);
	// int r = mbedtls_havege_random( &hs, buf, sizeof( buf ) );
	// if(r != 0 )
	// 	return LABS_ERR_UNKNOWN;
	// return LABS_ERR_SUCCESS;

	// https://github.com/ARMmbed/mbedtls/blob/development/programs/random/gen_random_ctr_drbg.c
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_entropy_context entropy;

	mbedtls_ctr_drbg_init(&ctr_drbg);
	mbedtls_entropy_init(&entropy);

	mbedtls_ctr_drbg_set_prediction_resistance(&ctr_drbg, MBEDTLS_CTR_DRBG_PR_OFF);
	if(mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)"RANDOM_GEN", 10) != 0)
	{
		return LABS_ERR_UNKNOWN;
	}
	if(mbedtls_ctr_drbg_random(&ctr_drbg, buf, buf_size) != 0)
	{
		return LABS_ERR_UNKNOWN;
	}

	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);
	return LABS_ERR_SUCCESS;

#else
	int r = RAND_bytes(buf, (int)buf_size);
	if(!r)
		return LABS_ERR_UNKNOWN;
	return LABS_ERR_SUCCESS;
#endif
}

LABS_EXPORT uint32_t labs_random_32()
{
	uint32_t rand_number;
	labs_random_bytes_crypt((uint8_t *)(&rand_number), 4);
	return rand_number;
}
