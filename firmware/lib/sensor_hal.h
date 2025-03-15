#ifndef AL_SENSOR_HAL_H
#define AL_SENSOR_HAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
  bool (*transfer)(uint8_t target, uint8_t* wd, size_t wl, uint8_t* rd, size_t rl);
  void (*delay)(uint32_t ms);
  void (*debug)(const char* fmt);
} al_sensor_ops_t;

typedef struct {
  uint16_t co2;
  uint16_t tmp;
  uint16_t hum;
  uint16_t voc;
  uint16_t nox;
  uint32_t prs;
} al_sensor_raw_t;

void al_sensor_wire(al_sensor_ops_t ops);

bool al_sensor_reset();

bool al_sensor_ready();

bool al_sensor_read(al_sensor_raw_t* raw);

bool al_sensor_sleep();

bool al_sensor_wake();

#endif  // AL_SENSOR_HAL_H
