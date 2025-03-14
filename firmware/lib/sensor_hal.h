#ifndef AL_SENSOR_HAL_H
#define AL_SENSOR_HAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
  bool (*transfer)(uint8_t target, uint8_t * wd, size_t wl, uint8_t * rd, size_t rl);
  int64_t (*millis)();
  void (*delay)(uint32_t ms);
  void (*log)(const char * fmt, ...);
} al_sensor_ops_t;

typedef struct {
  float co2;  // ppm
  float tmp;  // °C
  float hum;  // % rH
  uint16_t voc;  // raw
  uint16_t nox;  // raw
  float prs;  // hPa
} al_sensor_raw_t;

void al_sensor_wire(al_sensor_ops_t ops);

bool al_sensor_reset();

bool al_sensor_ready();

bool al_sensor_read(al_sensor_raw_t * raw);

bool al_sensor_sleep();

bool al_sensor_wake();

#endif // AL_SENSOR_HAL_H
