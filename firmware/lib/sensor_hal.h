#ifndef AL_SENSOR_HAL_H
#define AL_SENSOR_HAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
  bool (*transfer)(uint8_t target, uint8_t* wd, size_t wl, uint8_t* rd, size_t rl);
  void (*delay)(uint32_t ms);
  void (*debug)(const char* fmt);
  int64_t (*epoch)();
} al_sensor_hal_ops_t;

typedef struct {
  int64_t epoch;
  uint16_t co2;
  uint16_t tmp;
  uint16_t hum;
  uint16_t voc;
  uint16_t nox;
  uint32_t prs;
} al_sensor_hal_data_t;

void al_sensor_hal_wire(al_sensor_hal_ops_t ops);

bool al_sensor_hal_config(bool low_power);

bool al_sensor_hal_ready();

bool al_sensor_hal_read(al_sensor_hal_data_t* data);

#endif  // AL_SENSOR_HAL_H
