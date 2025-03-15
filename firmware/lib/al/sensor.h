#ifndef AL_SENSOR_H
#define AL_SENSOR_H

#include <stdbool.h>

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
typedef struct {
  bool ok;
  float co2;  // ppm
  float tmp;  // °C
  float hum;  // % rH
  float voc;  // indexed
  float nox;  // indexed
  float prs;  // hPa
} al_sensor_sample_t;

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
 * @note Might be zero, check the `ok` field.
 * @return The sensor sample.
 */
al_sensor_sample_t al_sensor_get();

/**
 * Await the next sensor sample.
 *
 * @return The sensor sample.
 */
al_sensor_sample_t al_sensor_next();

/**
 * Queries the sensor history.
 *
 * @param sensor The sensor.
 * @return The sensor history.
 */
al_sensor_history_t al_sensor_query(al_sensor_t sensor);

#endif  // AL_SENSOR_H
