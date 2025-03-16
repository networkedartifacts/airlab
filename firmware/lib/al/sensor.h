#ifndef AL_SENSOR_H
#define AL_SENSOR_H

#include <stdbool.h>
#include <stddef.h>

#define AL_SENSOR_HIST 8

/**
 * The available sensors.
 */
typedef enum {
  AL_SENSOR_CO2,
  AL_SENSOR_TMP,
  AL_SENSOR_HUM,
  AL_SENSOR_VOC,
  AL_SENSOR_NOX,
  AL_SENSOR_PRS,
} al_sensor_t;

/**
 * A sensor sample.
 */
typedef struct __attribute__((packed)) {
  bool ok;
  float co2;  // ppm
  float tmp;  // °C
  float hum;  // % rH
  float voc;  // indexed
  float nox;  // indexed
  float prs;  // hPa
} al_sensor_sample_t;

/**
 * The available sample stores.
 */
typedef enum {
  AL_SENSOR_5S,
  AL_SENSOR_30S,
} al_sample_store_t;

/**
 * A sensor history.
 */
typedef struct {
  float values[AL_SENSOR_HIST];
  float min;
  float max;
} al_sensor_history_t;

/**
 * A sensor hook.
 */
typedef void (*al_sensor_hook_t)(al_sensor_sample_t sample);

/**
 * Configures a sensor hook.
 *
 * @param hook The sensor hook.
 */
void al_sensor_config(al_sensor_hook_t hook);

/**
 * Gets the current sensor sample.
 *
 * @note Might be zero right after reset, check the `ok` field.
 *
 * @return The sensor sample.
 */
al_sensor_sample_t al_sensor_get();

/**
 * Await the next sensor sample and return it.
 *
 * @return The sensor sample.
 */
al_sensor_sample_t al_sensor_next();

/**
 * Returns the sample count of a store.
 *
 * @param store The store.
 * @return The sample count.
 */
size_t al_sensor_count(al_sample_store_t store);

/**
 * Queries the sensor history.
 *
 * @param sensor The sensor.
 * @return The sensor history.
 */
al_sensor_history_t al_sensor_query(al_sensor_t sensor);

#endif  // AL_SENSOR_H
