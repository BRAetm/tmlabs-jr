// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_PACKETSTATS_H
#define LABS_PACKETSTATS_H

#include "thread.h"
#include "seqnum.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_packet_stats_t
{
	LabsMutex mutex;

	// For generations of packets, i.e. where we know the number of expected packets per generation
	uint64_t gen_received;
	uint64_t gen_lost;

	// For sequential packets, i.e. where packets are identified by a sequence number
	LabsSeqNum16 seq_min; // sequence number that was max at the last reset
	LabsSeqNum16 seq_max; // currently maximal sequence number
	uint64_t seq_received; // total received packets since the last reset
} LabsPacketStats;

LABS_EXPORT LabsErrorCode labs_packet_stats_init(LabsPacketStats *stats);
LABS_EXPORT void labs_packet_stats_fini(LabsPacketStats *stats);
LABS_EXPORT void labs_packet_stats_reset(LabsPacketStats *stats);
LABS_EXPORT void labs_packet_stats_push_generation(LabsPacketStats *stats, uint64_t received, uint64_t lost);
LABS_EXPORT void labs_packet_stats_push_seq(LabsPacketStats *stats, LabsSeqNum16 seq_num);
LABS_EXPORT void labs_packet_stats_get(LabsPacketStats *stats, bool reset, uint64_t *received, uint64_t *lost);

#ifdef __cplusplus
}
#endif

#endif // LABS_PACKETSTATS_H
