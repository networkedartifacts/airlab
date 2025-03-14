#ifndef AL_SENSOR_HAL_H
#define AL_SENSOR_HAL_H

typedef struct {
  float co2;  // ppm
  float tmp;  // °C
  float hum;  // % rH
  uint16_t voc;  // raw
  uint16_t nox;  // raw
  float prs;  // hPa
} al_sensor_raw_t;

void al_sensor_reset();

bool al_sensor_ready();

al_sensor_raw_t al_sensor_read();

void al_sensor_sleep();

void al_sensor_wake();

#endif // AL_SENSOR_HAL_H
