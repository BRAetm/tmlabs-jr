// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_CONGESTIONCONTROL_H
#define LABS_CONGESTIONCONTROL_H

#include "takion.h"
#include "thread.h"
#include "packetstats.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_congestion_control_t
{
	LabsTakion *takion;
	LabsPacketStats *stats;
	LabsThread thread;
	LabsBoolPredCond stop_cond;
	double packet_loss;
	double packet_loss_max;
} LabsCongestionControl;

LABS_EXPORT LabsErrorCode labs_congestion_control_start(LabsCongestionControl *control, LabsTakion *takion, LabsPacketStats *stats, double packet_loss_max);

/**
 * Stop control and join the thread
 */
LABS_EXPORT LabsErrorCode labs_congestion_control_stop(LabsCongestionControl *control);

#ifdef __cplusplus
}
#endif

#endif // LABS_CONGESTIONCONTROL_H
