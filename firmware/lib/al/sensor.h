#ifndef AL_SENSOR_H
#define AL_SENSOR_H

#include <stdint.h>

#include <al/sample.h>

/**
 * The available sensor rates.
 */
typedef enum {
  AL_SENSOR_RATE_5S,
  AL_SENSOR_RATE_30S,
  AL_SENSOR_RATE_60S,
} al_sensor_rate_t;

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
 * Set the sensor rate.
 *
 * @param rate The sensor rate.
 */
void al_sensor_set_rate(al_sensor_rate_t rate);

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
