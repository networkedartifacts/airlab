#ifndef AL_SAMPLE_H
#define AL_SAMPLE_H

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
 * A single sample.
 */
typedef struct __attribute__((packed)) {
  float co2;  // ppm
  float tmp;  // °C
  float hum;  // % rH
  float voc;  // indexed
  float nox;  // indexed
  float prs;  // hPa
} al_sample_t;

#endif  // AL_SAMPLE_H
