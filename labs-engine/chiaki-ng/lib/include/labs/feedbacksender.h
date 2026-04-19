// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_FEEDBACKSENDER_H
#define LABS_FEEDBACKSENDER_H

#include "controller.h"
#include "takion.h"
#include "thread.h"
#include "common.h"

#define LABS_FEEDBACK_HISTORY_PACKET_BUF_SIZE 0x300
#define LABS_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE 0x40

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_feedback_sender_t
{
	LabsLog *log;
	LabsTakion *takion;
	LabsThread thread;

	LabsSeqNum16 state_seq_num;

	LabsSeqNum16 history_seq_num;
	LabsFeedbackHistoryBuffer history_buf;
	uint8_t history_packets[LABS_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE][LABS_FEEDBACK_HISTORY_PACKET_BUF_SIZE];
	size_t history_packet_sizes[LABS_FEEDBACK_HISTORY_PACKET_QUEUE_SIZE];
	size_t history_packet_begin;
	size_t history_packet_len;

	bool should_stop;
	LabsControllerState controller_state_prev;
	LabsControllerState controller_state_history_prev;
	LabsControllerState controller_state;
	bool controller_state_changed;
	bool history_dirty;
	LabsMutex state_mutex;
	LabsCond state_cond;
} LabsFeedbackSender;

LABS_EXPORT LabsErrorCode labs_feedback_sender_init(LabsFeedbackSender *feedback_sender, LabsTakion *takion);
LABS_EXPORT void labs_feedback_sender_fini(LabsFeedbackSender *feedback_sender);
LABS_EXPORT LabsErrorCode labs_feedback_sender_set_controller_state(LabsFeedbackSender *feedback_sender, LabsControllerState *state);

#ifdef __cplusplus
}
#endif

#endif // LABS_FEEDBACKSENDER_H
