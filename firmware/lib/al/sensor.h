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
 * Set the gas grace period, which makes the gas window power dependent: while
 * USB powered, and for the given number of seconds after power loss, the
 * SGP41 runs continuously at full quality and the configured gas window
 * applies only afterwards. Full quality operation resumes as soon as power
 * returns. When zero (the default), the gas window applies unconditionally.
 * The grace is evaluated once per second while awake and on every wake up
 * from deep sleep, so transitions during sleep are applied on the next wake.
 *
 * @param seconds The grace period in seconds (0 = window always applies).
 */
void al_sensor_set_gas_grace(int32_t seconds);

/**
 * Set the PM measurement rate on battery. While USB powered, the PM sensor
 * follows the sensor interval (continuously below the duty cycling minimum,
 * duty cycled otherwise). On battery, the sensor stays off and its cache is
 * refreshed with burst measurements at the given rate, either while awake or
 * by extending a wake before deep sleep. When zero (the default), no PM
 * measurements are taken on battery.
 *
 * @param seconds The battery measurement rate in seconds (0 = off).
 */
void al_sensor_set_pm_rate(int32_t seconds);

/**
 * Prepares the PM sensor for deep sleep: performs a final burst measurement
 * if one is due (extending the wake by its duration) and idles the sensor.
 */
void al_sensor_pm_prepare();

/**
 * Returns the seconds until the next PM burst measurement is due, or
 * INT32_MAX if none are scheduled.
 */
int32_t al_sensor_pm_due();

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
