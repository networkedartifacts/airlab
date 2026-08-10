#ifndef AL_PM_H
#define AL_PM_H

#include <stdbool.h>
#include <stdint.h>

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

/**
 * Returns whether a sensor has been detected at boot.
 */
bool al_pm_present();

/**
 * The measurement modes.
 */
typedef enum {
  AL_PM_IDLE,
  AL_PM_CONTINUOUS,
  AL_PM_CYCLED,
} al_pm_mode_t;

/**
 * The cadence below which the sensor should measure continuously, as duty
 * cycling requires the period to exceed the 10s integration time.
 */
#define AL_PM_CYCLE_MIN 30

/**
 * Request the given measurement mode, applied asynchronously.
 *
 * @param mode The measurement mode.
 * @param period The duty cycling period in seconds (cycled mode only).
 * @param ttl The cache lifetime of readings in seconds.
 */
void al_pm_run(al_pm_mode_t mode, int32_t period, int32_t ttl);

/**
 * Request a burst measurement over the sensors integration time to refresh
 * the cache, executed asynchronously once the sensor is idle.
 *
 * @param ttl The cache lifetime of the reading in seconds.
 */
void al_pm_burst(int32_t ttl);

/**
 * Awaits the application of the requested mode and completion of requested
 * bursts, bounded in case the sensor persistently fails.
 */
void al_pm_flush();

/**
 * Returns the seconds since the last cached reading, or INT32_MAX if there
 * is none.
 */
int32_t al_pm_age();

/**
 * Returns the cached PM2.5 sample value (ug/m3 shifted by 10) if the given
 * epoch falls within the cache lifetime, or -1 if no reading is available.
 *
 * @param epoch The epoch time in milliseconds.
 */
int16_t al_pm_sample(int64_t epoch);

#endif  // AL_PM_H
