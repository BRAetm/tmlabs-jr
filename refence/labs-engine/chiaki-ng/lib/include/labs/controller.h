// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_CONTROLLER_H
#define LABS_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum labs_controller_button_t
{
	LABS_CONTROLLER_BUTTON_CROSS 		= (1 << 0),
	LABS_CONTROLLER_BUTTON_MOON 		= (1 << 1),
	LABS_CONTROLLER_BUTTON_BOX 		= (1 << 2),
	LABS_CONTROLLER_BUTTON_PYRAMID 	= (1 << 3),
	LABS_CONTROLLER_BUTTON_DPAD_LEFT 	= (1 << 4),
	LABS_CONTROLLER_BUTTON_DPAD_RIGHT = (1 << 5),
	LABS_CONTROLLER_BUTTON_DPAD_UP 	= (1 << 6),
	LABS_CONTROLLER_BUTTON_DPAD_DOWN 	= (1 << 7),
	LABS_CONTROLLER_BUTTON_L1 		= (1 << 8),
	LABS_CONTROLLER_BUTTON_R1 		= (1 << 9),
	LABS_CONTROLLER_BUTTON_L3			= (1 << 10),
	LABS_CONTROLLER_BUTTON_R3			= (1 << 11),
	LABS_CONTROLLER_BUTTON_OPTIONS 	= (1 << 12),
	LABS_CONTROLLER_BUTTON_SHARE 		= (1 << 13),
	LABS_CONTROLLER_BUTTON_TOUCHPAD	= (1 << 14),
	LABS_CONTROLLER_BUTTON_PS			= (1 << 15)
} LabsControllerButton;

#define LABS_CONTROLLER_BUTTONS_COUNT 16

typedef enum labs_controller_analog_button_t
{
	// must not overlap with LabsControllerButton
	LABS_CONTROLLER_ANALOG_BUTTON_L2 = (1 << 16),
	LABS_CONTROLLER_ANALOG_BUTTON_R2 = (1 << 17)
} LabsControllerAnalogButton;

typedef struct labs_controller_touch_t
{
	uint16_t x, y;
	int8_t id; // -1 = up
} LabsControllerTouch;

#define LABS_CONTROLLER_TOUCHES_MAX 2

typedef struct labs_controller_state_t
{
	/**
	 * Bitmask of LabsControllerButton
	 */
	uint32_t buttons;

	uint8_t l2_state;
	uint8_t r2_state;

	int16_t left_x;
	int16_t left_y;
	int16_t right_x;
	int16_t right_y;

	uint8_t touch_id_next;
	LabsControllerTouch touches[LABS_CONTROLLER_TOUCHES_MAX];

	float gyro_x, gyro_y, gyro_z;
	float accel_x, accel_y, accel_z;
	float orient_x, orient_y, orient_z, orient_w;
} LabsControllerState;

LABS_EXPORT void labs_controller_state_set_idle(LabsControllerState *state);

/**
 * @return A non-negative newly allocated touch id allocated or -1 if there are no slots left
 */
LABS_EXPORT int8_t labs_controller_state_start_touch(LabsControllerState *state, uint16_t x, uint16_t y);

LABS_EXPORT void labs_controller_state_stop_touch(LabsControllerState *state, uint8_t id);

LABS_EXPORT void labs_controller_state_set_touch_pos(LabsControllerState *state, uint8_t id, uint16_t x, uint16_t y);

LABS_EXPORT bool labs_controller_state_equals(LabsControllerState *a, LabsControllerState *b);

/**
 * Union of two controller states.
 * Ignores gyro, accel and orient, instead choosing first controller with motion data
 * Combining data would lead to unsatisfactory results / orientation values that won't work
 */
LABS_EXPORT void labs_controller_state_or(LabsControllerState *out, LabsControllerState *a, LabsControllerState *b);

#ifdef __cplusplus
}
#endif

#endif // LABS_CONTROLLER_H
