// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/packetstats.h>
#include <labs/log.h>
#include <assert.h>

LABS_EXPORT LabsErrorCode labs_packet_stats_init(LabsPacketStats *stats)
{
	LabsErrorCode err = labs_mutex_init(&stats->mutex, false);
	if(err != LABS_ERR_SUCCESS)
		return err;
	err = labs_mutex_lock(&stats->mutex);
	assert(err == LABS_ERR_SUCCESS);
	stats->gen_received = 0;
	stats->gen_lost = 0;
	stats->seq_min = 0;
	stats->seq_max = 0;
	stats->seq_received = 0;
	err = labs_mutex_unlock(&stats->mutex);
	return err;
}

LABS_EXPORT void labs_packet_stats_fini(LabsPacketStats *stats)
{
	labs_mutex_fini(&stats->mutex);
}

static void reset_stats(LabsPacketStats *stats)
{
	stats->gen_received = 0;
	stats->gen_lost = 0;
	stats->seq_min = stats->seq_max;
	stats->seq_received = 0;
}

LABS_EXPORT void labs_packet_stats_reset(LabsPacketStats *stats)
{
	labs_mutex_lock(&stats->mutex);
	reset_stats(stats);
	labs_mutex_unlock(&stats->mutex);
}

LABS_EXPORT void labs_packet_stats_push_generation(LabsPacketStats *stats, uint64_t received, uint64_t lost)
{
	labs_mutex_lock(&stats->mutex);
	stats->gen_received += received;
	stats->gen_lost += lost;
	labs_mutex_unlock(&stats->mutex);
}

LABS_EXPORT void labs_packet_stats_push_seq(LabsPacketStats *stats, LabsSeqNum16 seq_num)
{
	stats->seq_received++;
	if(labs_seq_num_16_gt(seq_num, stats->seq_max))
		stats->seq_max = seq_num;
}

LABS_EXPORT void labs_packet_stats_get(LabsPacketStats *stats, bool reset, uint64_t *received, uint64_t *lost)
{
	labs_mutex_lock(&stats->mutex);

	// gen
	*received = stats->gen_received;
	*lost = stats->gen_lost;

	//LABS_LOGD(NULL, "gen received: %llu, lost: %llu",
	//		(unsigned long long)stats->gen_received,
	//		(unsigned long long)stats->gen_lost);

	// seq
	uint64_t seq_diff = stats->seq_max - stats->seq_min; // overflow on purpose if max < min
	uint64_t seq_lost = stats->seq_received > seq_diff ? seq_diff : seq_diff - stats->seq_received;
	*received += stats->seq_received;
	*lost += seq_lost;

	//LABS_LOGD(NULL, "seq received: %llu, lost: %llu",
	//		(unsigned long long)stats->seq_received,
	//		(unsigned long long)seq_lost);

	if(reset)
		reset_stats(stats);
	labs_mutex_unlock(&stats->mutex);
}
