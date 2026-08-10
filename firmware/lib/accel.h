#ifndef AL_ACCEL_IMPL_H
#define AL_ACCEL_IMPL_H

#include <stdbool.h>

#include <al/accel.h>

/* FXLS8974CF */

// returns whether the chip is present
bool al_accel_fxl_detect();

// initialize the chip for orientation detection
void al_accel_fxl_init(bool reset);

// quiesce the chip, so it releases the shared interrupt line
void al_accel_fxl_quiesce();

// update state from the chip, returns false on read errors
bool al_accel_fxl_check(al_accel_state_t *state);

/* LIS2DH12 */

// returns whether the chip is present
bool al_accel_lis_detect();

// initialize the chip for orientation detection
void al_accel_lis_init(bool reset);

// update state from the chip, returns false on read errors
bool al_accel_lis_check(al_accel_state_t *state);

#endif  // AL_ACCEL_IMPL_H
