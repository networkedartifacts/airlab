#ifndef AL_SENSOR_H
#define AL_SENSOR_H

#include <stddef.h>

#include <al/sample.h>

#define AL_SENSOR_NUM_SHORT 180  // 15min-1.5h (5s/30s)
#define AL_SENSOR_NUM_LONG 300   // 2.5h-75h (30s/15m)

/**
 * The available stores.
 */
typedef enum {
  AL_SENSOR_SHORT,
  AL_SENSOR_LONG,
} al_sensor_store_t;

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
 * Returns the interval at which short term samples are moved to long term storage.
 *
 * @return The interval in seconds.
 */
int al_sensor_get_interval();

/**
 * Sets the interval at which short term samples are moved to long term storage.
 *
 * @param interval The interval in seconds.
 */
void al_sensor_set_interval(int interval);

/**
 * Returns the first (oldest) sensor sample.
 *
 * @note Might be zero right after reset, check the `co2` field.
 *
 * @return The first sensor sample.
 */
al_sample_t al_sensor_first();

/**
 * Returns the last (newest) sensor sample.
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
size_t al_sensor_count(al_sensor_store_t store);

/**
 * Gets a sensor sample from a store.
 *
 * @param store The store.
 * @param num The sample index if positive or the offset from the last sample if negative.
 */
al_sample_t al_sensor_get(al_sensor_store_t store, int num);

/**
 * Returns a combined sample source for both stores.
 *
 * @return The sample source.
 */
al_sample_source_t al_sensor_source();

#endif  // AL_SENSOR_H
