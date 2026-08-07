#ifndef AL_ULP_SHARED_H
#define AL_ULP_SHARED_H

#include <stdint.h>
#include <stddef.h>

#include "../sensor_hal.h"

typedef enum { AL_ULP_TYPE_ERROR } al_ulp_log_type_t;

/**
 * Performs a sensor HAL transfer using the RTC I2C peripheral. Used by the ULP
 * program and the main CPU before the main I2C driver is available.
 */
al_sensor_hal_err_t al_ulp_transfer(uint8_t target, uint8_t* wd, size_t wl, uint8_t* rd, size_t rl);

typedef struct {
  int32_t time;  // ms
  al_ulp_log_type_t type;
  int64_t value;
} al_ulp_log_t;

#endif  // AL_ULP_SHARED_H
