#ifndef AL_SENSOR_PM_H
#define AL_SENSOR_PM_H

#include <stdbool.h>
#include <stdint.h>

// the particulate matter measurement state, only PM2.5 is a specified
// measurement, PM1 and PM10 are estimates derived from it
typedef struct {
  bool valid;         // whether a measurement has been received
  float pm1;          // PM1 mass concentration in ug/m3
  float pm2_5;        // PM2.5 mass concentration in ug/m3
  float pm10;         // PM10 mass concentration in ug/m3
  bool obstructed;    // whether the sensor is obstructed and cannot measure
  bool out_of_range;  // whether the PM2.5 value is outside the measurement range
} al_sensor_pm_state_t;

// the particulate matter hook, invoked on every new measurement
typedef void (*al_sensor_pm_hook_t)(al_sensor_pm_state_t state);

// initialize the particulate matter sensor
void al_sensor_pm_init(bool reset);

// configure the particulate matter sensor
void al_sensor_pm_config(al_sensor_pm_hook_t hook);

// returns the cached particulate matter state
al_sensor_pm_state_t al_sensor_pm_get();

// the minimum duty cycling period, as duty cycling requires the period to
// exceed the 10s integration time
#define AL_SENSOR_PM_CYCLE_MIN 30

// request a duty cycled measurement with the given period in seconds, or an
// idle sensor if zero, applied asynchronously, with the cache lifetime of
// readings in seconds
void al_sensor_pm_run(int32_t period, int32_t ttl);

// request a burst measurement over the sensors integration time to refresh the
// cache with the given lifetime, executed asynchronously once the sensor is
// idle, discarded if a measurement mode is requested or the sensor is suspended
void al_sensor_pm_burst(int32_t ttl);

// awaits the application of the requested mode and completion of requested
// bursts, bounded in case the sensor persistently fails
void al_sensor_pm_flush();

// returns the seconds since the last cached reading, or INT32_MAX if there is
// none
int32_t al_sensor_pm_age();

// returns the cached PM2.5 sample value (ug/m3 shifted by 10) if the given
// epoch (in milliseconds) falls within the cache lifetime, or -1 if no reading
// is available
int16_t al_sensor_pm_sample(int64_t epoch);

#endif  // AL_SENSOR_PM_H
