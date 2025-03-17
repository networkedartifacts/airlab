#ifndef AL_SENSOR_H
#define AL_SENSOR_H

#include <stddef.h>

#include <al/sample.h>

#define AL_SENSOR_NUM_5S 60    // 5min
#define AL_SENSOR_NUM_30S 240  // 2h

/**
 * The available sample stores.
 */
typedef enum {
  AL_SENSOR_5S,
  AL_SENSOR_30S,
} al_sample_store_t;

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
 * Returns the last sensor sample.
 *
 * @note Might be zero right after reset, check the `co2` field.
 *
 * @return The sensor sample.
 */
al_sample_t al_sensor_last();

/**
 * Await the next sensor sample and return it.
 *
 * @return The sensor sample.
 */
al_sample_t al_sensor_next();

/**
 * Returns the sample count of a store.
 *
 * @param store The store.
 * @return The sample count.
 */
size_t al_sensor_count(al_sample_store_t store);

/**
 * Gets a sensor sample from a store.
 *
 * @param store The store.
 * @param num The sample index if positive or the offset from the last sample if negative.
 */
al_sample_t al_sensor_get(al_sample_store_t store, int num);

/**
 * Queries the sensor history.
 *
 * @param store The store.
 * @param sensor The sensor.
 * @param num The sample index if positive or the offset from the last sample if negative.
 * @param values The sensor values.
 * @param min The minimum sensor value.
 * @param max The maximum sensor value.
 * @return The sensor history.
 */
size_t al_sensor_query(al_sample_store_t store, al_sensor_t sensor, int num, float *values, float *min, float *max);

#endif  // AL_SENSOR_H
