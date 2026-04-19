// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_SENKUSHA_H
#define LABS_SENKUSHA_H

#include "takion.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_session_t LabsSession;

typedef struct senkusha_t
{
	LabsSession *session;
	LabsLog *log;
	LabsTakion takion;

	int state;
	bool state_finished;
	bool state_failed;
	bool should_stop;
	LabsSeqNum32 data_ack_seq_num_expected;
	uint64_t pong_time_us;
	uint16_t ping_test_index;
	uint16_t ping_index;
	uint32_t ping_tag;
	uint32_t mtu_id;

	/**
	 * signaled on change of state_finished or should_stop
	 */
	LabsCond state_cond;

	/**
	 * protects state, state_finished, state_failed and should_stop
	 */
	LabsMutex state_mutex;
} LabsSenkusha;

LABS_EXPORT LabsErrorCode labs_senkusha_init(LabsSenkusha *senkusha, LabsSession *session);
LABS_EXPORT void labs_senkusha_fini(LabsSenkusha *senkusha);
LABS_EXPORT LabsErrorCode labs_senkusha_run(LabsSenkusha *senkusha, uint32_t *mtu_in, uint32_t *mtu_out, uint64_t *rtt_us, labs_socket_t *sock);

#ifdef __cplusplus
}
#endif

#endif // LABS_SENKUSHA_H
