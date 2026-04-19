// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_FEEDBACK_H
#define LABS_FEEDBACK_H

#include "common.h"
#include "log.h"
#include "controller.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_feedback_state_t
{
	float gyro_x, gyro_y, gyro_z;
	float accel_x, accel_y, accel_z;
	float orient_x, orient_y, orient_z, orient_w;
	int16_t left_x;
	int16_t left_y;
	int16_t right_x;
	int16_t right_y;
} LabsFeedbackState;

#define LABS_FEEDBACK_STATE_BUF_SIZE_MAX 0x1c

#define LABS_FEEDBACK_STATE_BUF_SIZE_V9 0x19

/**
 * @param buf buffer of at least LABS_FEEDBACK_STATE_BUF_SIZE_V9
 */
LABS_EXPORT void labs_feedback_state_format_v9(uint8_t *buf, LabsFeedbackState *state);

#define LABS_FEEDBACK_STATE_BUF_SIZE_V12 0x1c

/**
 * @param buf buffer of at least LABS_FEEDBACK_STATE_BUF_SIZE_V12
 */
LABS_EXPORT void labs_feedback_state_format_v12(uint8_t *buf, LabsFeedbackState *state);

#define LABS_HISTORY_EVENT_SIZE_MAX 0x5

typedef struct labs_feedback_history_event_t
{
	uint8_t buf[LABS_HISTORY_EVENT_SIZE_MAX];
	size_t len;
} LabsFeedbackHistoryEvent;

/**
 * @param button LabsControllerButton or LabsControllerAnalogButton
 * @param state 0x0 for not pressed, 0xff for pressed, intermediate values for analog triggers
 */
LABS_EXPORT LabsErrorCode labs_feedback_history_event_set_button(LabsFeedbackHistoryEvent *event, uint64_t button, uint8_t state);

/**
 * @param pointer_id identifier for the touch from 0 to 127
 * @param x from 0 to 1920
 * @param y from 0 to 942
 */
LABS_EXPORT void labs_feedback_history_event_set_touchpad(LabsFeedbackHistoryEvent *event,
		bool down, uint8_t pointer_id, uint16_t x, uint16_t y);

/**
 * Ring buffer of LabsFeedbackHistoryEvent
 */
typedef struct labs_feedback_history_buffer_t
{
	LabsFeedbackHistoryEvent *events;
	size_t size;
	size_t begin;
	size_t len;
} LabsFeedbackHistoryBuffer;

LABS_EXPORT LabsErrorCode labs_feedback_history_buffer_init(LabsFeedbackHistoryBuffer *feedback_history_buffer, size_t size);
LABS_EXPORT void labs_feedback_history_buffer_fini(LabsFeedbackHistoryBuffer *feedback_history_buffer);

/**
 * @param buf_size Pointer to the allocated size of buf, will contain the written size after a successful formatting.
 */
LABS_EXPORT LabsErrorCode labs_feedback_history_buffer_format(LabsFeedbackHistoryBuffer *feedback_history_buffer, uint8_t *buf, size_t *buf_size);

/**
 * Push an event to the front of the buffer
 */
LABS_EXPORT void labs_feedback_history_buffer_push(LabsFeedbackHistoryBuffer *feedback_history_buffer, LabsFeedbackHistoryEvent *event);

#ifdef __cplusplus
}
#endif

#endif // LABS_FEEDBACK_H
