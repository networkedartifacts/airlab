#ifndef AL_SENSOR_HAL_H
#define AL_SENSOR_HAL_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
  AL_SENSOR_HAL_NORMAL,     // 5s
  AL_SENSOR_HAL_LOW_POWER,  // 30s
  AL_SENSOR_HAL_MANUAL,
  AL_SENSOR_HAL_SLEEP
} al_sensor_hal_mode_t;

typedef enum {
  // bare
  AL_SENSOR_HAL_OK = 0,
  AL_SENSOR_HAL_BUSY = 1 << 0,
  // flagged
  AL_SENSOR_HAL_ERR_TRANSFER = 1 << 1,
  AL_SENSOR_HAL_ERR_CHECKSUM = 1 << 2,
  AL_SENSOR_HAL_ERR_TIMEOUT = 1 << 3,
  AL_SENSOR_HAL_ERR_MODE = 1 << 4,
  // flags
  AL_SENSOR_HAL_ERR_SCD41 = 1 << 11,
  AL_SENSOR_HAL_ERR_SGP41 = 1 << 12,
  AL_SENSOR_HAL_ERR_LPS22 = 1 << 13,
} al_sensor_hal_err_t;

typedef struct {
  al_sensor_hal_err_t (*transfer)(uint8_t target, uint8_t* wd, size_t wl, uint8_t* rd, size_t rl);
  void (*delay)(uint32_t ms);
  int64_t (*epoch)();
} al_sensor_hal_ops_t;

typedef struct {
  al_sensor_hal_mode_t mode;
  int rate;
  int64_t next;
} al_sensor_hal_state_t;

typedef struct {
  int64_t epoch;
  uint16_t co2;
  uint16_t tmp;
  uint16_t hum;
  uint16_t voc;
  uint16_t nox;
  uint32_t prs;
} al_sensor_hal_data_t;

void al_sensor_hal_init(al_sensor_hal_ops_t ops, al_sensor_hal_state_t* state);
al_sensor_hal_err_t al_sensor_hal_config(al_sensor_hal_mode_t mode, int rate);
al_sensor_hal_err_t al_sensor_hal_ready();
al_sensor_hal_err_t al_sensor_hal_read(al_sensor_hal_data_t* data);
al_sensor_hal_state_t al_sensor_hal_dump();

#endif  // AL_SENSOR_HAL_H
