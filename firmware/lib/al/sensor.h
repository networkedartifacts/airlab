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
 * clamped to 5-300 seconds.
 *
 * @param seconds The measurement interval in seconds.
 */
void al_sensor_set_interval(int32_t seconds);

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
