// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <labs/congestioncontrol.h>

#define CONGESTION_CONTROL_INTERVAL_MS 200

static void *congestion_control_thread_func(void *user)
{
	LabsCongestionControl *control = user;
	labs_thread_set_affinity(LABS_THREAD_NAME_CONGESTION);

	LabsErrorCode err = labs_bool_pred_cond_lock(&control->stop_cond);
	if(err != LABS_ERR_SUCCESS)
		return NULL;

	while(true)
	{
		err = labs_bool_pred_cond_timedwait(&control->stop_cond, CONGESTION_CONTROL_INTERVAL_MS);
		if(err != LABS_ERR_TIMEOUT)
			break;

		uint64_t received;
		uint64_t lost;
		labs_packet_stats_get(control->stats, true, &received, &lost);
		LabsTakionCongestionPacket packet = { 0 };
		uint64_t total = received + lost;
		control->packet_loss = total > 0 ? (double)lost / total : 0;
		if(control->packet_loss > control->packet_loss_max)
		{
			LABS_LOGD(control->takion->log, "Clamping reported packet loss: measured=%.1f%% reported_max=%.1f%%",
				control->packet_loss * 100.0, control->packet_loss_max * 100.0);
			lost = total * control->packet_loss_max;
			received = total - lost;
		}
		packet.received = (uint16_t)received;
		packet.lost = (uint16_t)lost;
		LABS_LOGV(control->takion->log, "Sending Congestion Control Packet, received: %u, lost: %u",
			(unsigned int)packet.received, (unsigned int)packet.lost);
		labs_takion_send_congestion(control->takion, &packet);
	}

	labs_bool_pred_cond_unlock(&control->stop_cond);
	return NULL;
}

LABS_EXPORT LabsErrorCode labs_congestion_control_start(LabsCongestionControl *control, LabsTakion *takion, LabsPacketStats *stats, double packet_loss_max)
{
	control->takion = takion;
	control->stats = stats;
	control->packet_loss_max = packet_loss_max;
	control->packet_loss = 0;

	LabsErrorCode err = labs_bool_pred_cond_init(&control->stop_cond);
	if(err != LABS_ERR_SUCCESS)
		return err;

	err = labs_thread_create(&control->thread, congestion_control_thread_func, control);
	if(err != LABS_ERR_SUCCESS)
	{
		labs_bool_pred_cond_fini(&control->stop_cond);
		return err;
	}

	labs_thread_set_name(&control->thread, "Labs Congestion Control");

	return LABS_ERR_SUCCESS;
}

LABS_EXPORT LabsErrorCode labs_congestion_control_stop(LabsCongestionControl *control)
{
	LabsErrorCode err = labs_bool_pred_cond_signal(&control->stop_cond);
	if(err != LABS_ERR_SUCCESS)
		return err;

	err = labs_thread_join(&control->thread, NULL);
	if(err != LABS_ERR_SUCCESS)
		return err;
	control->thread.thread = 0;

	return labs_bool_pred_cond_fini(&control->stop_cond);
}
