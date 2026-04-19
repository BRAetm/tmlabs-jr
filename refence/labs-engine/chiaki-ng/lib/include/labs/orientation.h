// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_ORIENTATION_H
#define LABS_ORIENTATION_H

#include "common.h"
#include "controller.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Quaternion orientation from accelerometer and gyroscope
 * using Madgwick's algorithm.
 * See: http://www.x-io.co.uk/node/8#open_source_ahrs_and_imu_algorithms
 */
typedef struct labs_orientation_t
{
	float x, y, z, w;
} LabsOrientation;

typedef struct labs_accel_new_zero
{
	float accel_x, accel_y, accel_z;
} LabsAccelNewZero;

LABS_EXPORT void labs_orientation_init(LabsOrientation *orient);
LABS_EXPORT void labs_orientation_update(LabsOrientation *orient,
		float gx, float gy, float gz, float ax, float ay, float az, float beta, float time_step_sec);

/**
 * Extension of LabsOrientation, also tracking an absolute timestamp and the current gyro/accel state
 */
typedef struct labs_orientation_tracker_t
{
	float gyro_x, gyro_y, gyro_z;
	float accel_x, accel_y, accel_z;
	LabsOrientation orient;
	uint32_t timestamp;
	uint64_t sample_index;
} LabsOrientationTracker;

LABS_EXPORT void labs_orientation_tracker_init(LabsOrientationTracker *tracker);
LABS_EXPORT void labs_orientation_tracker_update(LabsOrientationTracker *tracker,
		float gx, float gy, float gz, float ax, float ay, float az,
		LabsAccelNewZero *accel_zero, bool accel_zero_applied, uint32_t timestamp_us);
LABS_EXPORT void labs_orientation_tracker_apply_to_controller_state(LabsOrientationTracker *tracker,
		LabsControllerState *state);
LABS_EXPORT void labs_accel_new_zero_set_inactive(LabsAccelNewZero *accel_zero, bool real_accel);
LABS_EXPORT void labs_accel_new_zero_set_active(LabsAccelNewZero *accel_zero, float accel_x, float accel_y, float accel_z, bool real_accel);

#ifdef __cplusplus
}
#endif

#endif // LABS_ORIENTATION_H
