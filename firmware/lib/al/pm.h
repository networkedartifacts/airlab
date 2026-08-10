#ifndef AL_PM_H
#define AL_PM_H

#include <stdbool.h>

/**
 * The particulate matter measurement state. Only PM2.5 is a specified
 * measurement, PM1 and PM10 are estimates derived from it.
 *
 * @param valid Whether a measurement has been received.
 * @param pm1 PM1 mass concentration in ug/m3.
 * @param pm2_5 PM2.5 mass concentration in ug/m3.
 * @param pm10 PM10 mass concentration in ug/m3.
 * @param obstructed Whether the sensor is obstructed and cannot measure.
 * @param out_of_range Whether the PM2.5 value is outside the measurement range.
 */
typedef struct {
  bool valid;
  float pm1;
  float pm2_5;
  float pm10;
  bool obstructed;
  bool out_of_range;
} al_pm_state_t;

/**
 * The particulate matter hook.
 */
typedef void (*al_pm_hook_t)(al_pm_state_t state);

/**
 * Configure the particulate matter sensor.
 *
 * @param hook The hook invoked on every new measurement.
 */
void al_pm_config(al_pm_hook_t hook);

/**
 * Returns the cached particulate matter state.
 */
al_pm_state_t al_pm_get();

#endif  // AL_PM_H
