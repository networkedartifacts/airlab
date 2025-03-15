#ifndef AL_SENSOR_H
#define AL_SENSOR_H

#include <stdbool.h>

#define AL_SENSOR_HIST 8

typedef enum {
  AL_SENSOR_CO2,
  AL_SENSOR_TMP,
  AL_SENSOR_HUM,
  AL_SENSOR_VOC,
  AL_SENSOR_NOX,
  AL_SENSOR_PRS,
} al_sensor_mode_t;

typedef struct {
  bool ok;
  float co2;  // ppm
  float tmp;  // °C
  float hum;  // % rH
  float voc;  // indexed
  float nox;  // indexed
  float prs;  // hPa
} al_sensor_state_t;

typedef struct {
  float values[AL_SENSOR_HIST];
  float min;
  float max;
} al_sensor_hist_t;

typedef void (*al_sensor_hook_t)(al_sensor_state_t state);

void al_sensor_config(al_sensor_hook_t hook);
al_sensor_state_t al_sensor_get();
al_sensor_state_t al_sensor_next();
al_sensor_hist_t al_sensor_query(al_sensor_mode_t mode);

#endif  // AL_SENSOR_H
