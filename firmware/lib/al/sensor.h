#ifndef AL_SENSOR_H
#define AL_SENSOR_H

#include <stdint.h>

#include <al/sample.h>

/**
 * A sensor hook.
 */
typedef void (*al_sensor_hook_t)(al_sample_t sample);

/**
 * Configures a sensor hook.
 *
 * @param hook The sensor hook.
 */
void al_sensor_config(al_sensor_hook_t hook);

/**
 * Await the next sensor sample and return it.
 *
 * @return The sensor sample.
 */
al_sample_t al_sensor_next();

/**
 * Set the sensor measurement interval. The most efficient sensor mode with a
 * cadence not slower than the requested interval is selected: below 30s the
 * sensor measures periodically every 5s, below 60s every 30s, and from 60s on
 * single-shot measurements are taken at the requested interval. Values are
 * clamped to 5-600 seconds.
 *
 * @param seconds The measurement interval in seconds.
 */
void al_sensor_set_interval(int32_t seconds);

/**
 * Set the SGP41 active window per measurement cycle in manual mode. When zero
 * (the default), the SGP41 runs continuously with its heater always on, which
 * preserves full signal quality. When set, the sensor is duty-cycled: per
 * cycle it runs conditioning (max 10s) followed by raw measurements at 1Hz
 * until the reading is taken at the end of the window, after which the heater
 * is turned off. Duty cycling trades VOC/NOx signal quality for battery life
 * and must be opted into deliberately. The window is applied on the next
 * al_sensor_set_interval() call and clamped to 10 seconds at least and half
 * the measurement interval at most. A negative window disables the SGP41
 * entirely in all modes: the heater stays off, VOC/NOx are reported as zero
 * and the gas index algorithms are reset when re-enabled, behaving like a
 * cold start including the NOx run-in.
 *
 * @param seconds The active window in seconds (0 = continuous, <0 = off).
 */
void al_sensor_set_gas_window(int32_t seconds);

/**
 * Prepares the sensor for deep sleep. When duty cycling in manual mode the
 * SGP heater is turned off, so the ULP can cycle it around measurements. In
 * continuous operation the heater stays on.
 */
void al_sensor_sleep();

/**
 * Turns the sensor off: the SCD41 is powered down, the SGP heater is turned
 * off and the LPS22 is set to power-down mode. The sensor is reconfigured on
 * the next al_sensor_set_interval() call after wake up.
 */
void al_sensor_off();

/**
 * Returns the latest raw VOC reading from the sensor.
 *
 * @return The raw VOC value.
 */
uint16_t al_sensor_raw_voc();

/**
 * Returns the latest raw NOx reading from the sensor.
 *
 * @return The raw NOx value.
 */
uint16_t al_sensor_raw_nox();

#endif  // AL_SENSOR_H
