#ifndef AL_SENSOR_HAL_H
#define AL_SENSOR_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define AL_SENSOR_MSR_TIME 5000   // ms
#define AL_SENSOR_CND_TIME 9000   // ms (SGP41 conditioning time, below the 10s limit)
#define AL_SENSOR_MAX_HEAT 10000  // ms (grace beyond the active window before the heater is forced off)
#define AL_SENSOR_CYCLE_TIME (180 * 1000 - AL_SENSOR_MSR_TIME)  // manual interval from which the SCD is power-cycled

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
  bool condition;  // runs the SGP duty-cycle state machine (ULP only)
} al_sensor_hal_ops_t;

typedef struct {
  al_sensor_hal_mode_t mode;
  int interval;
  int duty;  // SGP active window per cycle in manual mode (ms, 0 = continuous, <0 = disabled in all modes)
  int64_t next;
  int64_t heat;
  int64_t raw;
  int64_t shot;
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
al_sensor_hal_err_t al_sensor_hal_config(al_sensor_hal_mode_t mode, int interval, int duty);
al_sensor_hal_err_t al_sensor_hal_heater_off();
bool al_sensor_hal_ready();
al_sensor_hal_err_t al_sensor_hal_read(al_sensor_hal_data_t* data);
al_sensor_hal_state_t al_sensor_hal_dump();

#endif  // AL_SENSOR_HAL_H
