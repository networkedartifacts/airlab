#ifndef AL_ACCEL_H
#define AL_ACCEL_H

#include <stdbool.h>
#include <stdint.h>

/**
 * The accelerometer state.
 *
 * @param front Whether the device is front facing.
 * @param rot The rotation of the device.
 * @param lock Whether the device is gimbal locked.
 */
typedef struct {
  bool front;
  uint16_t rot;
  bool lock;
} al_accel_state_t;

/**
 * Returns the cached accelerometer state.
 */
al_accel_state_t al_accel_get();

#endif  // AL_ACCEL_H
