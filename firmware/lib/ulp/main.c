#include <ulp_riscv.h>
#include <ulp_riscv_utils.h>
#include <ulp_riscv_i2c.h>
#include <hal/rtc_cntl_ll.h>

#include "../sensor_hal.h"

#include "shared.h"

#define READINGS 64  // 320s at 5s rate
#define LOGS 32
#define DEBUG false

// use our own constant to avoid software floating point calculations
#define MS_CYCLES 17500
static_assert(MS_CYCLES == ULP_RISCV_CYCLES_PER_MS, "cycles mismatch");

static al_sensor_hal_data_t data = {0};

volatile al_sensor_hal_state_t state = {0};
volatile uint64_t offset = 0;
volatile int64_t start = 0;
volatile al_sensor_hal_data_t readings[READINGS] = {0};
volatile int num_readings = 0;
volatile al_ulp_log_t logs[LOGS];
volatile int num_logs = 0;
volatile uint32_t handover = 0;
volatile uint32_t running = 0;
volatile uint32_t ack = 0;

static void delay(uint32_t ms) {
  // perform delay
  ulp_riscv_delay_cycles(ms * MS_CYCLES);
}

static int millis() {
  // get time
  uint64_t now = rtc_cntl_ll_get_rtc_time();

  // get difference
  uint64_t time = now - offset;
  if (now < offset) {
    time = (UINT64_MAX - offset) + now;
  }

  // calculate milliseconds
  time *= 6667;         // ns
  time /= 1000 * 1000;  // ms

  return (int)time;
}

static int64_t epoch() {
  // calculate epoch in milliseconds
  return start + (int64_t)millis();
}

static void log(al_ulp_log_type_t type, int64_t value) {
  // stop if full
  if (num_logs >= LOGS) {
    return;
  }

  // otherwise, store log
  logs[num_logs].time = millis();
  logs[num_logs].type = type;
  logs[num_logs].value = value;
  num_logs++;
}

static void run(void) {
  // check if ready
  bool ready = al_sensor_hal_ready();
  if (!ready) {
    return;
  }

  // read sensor
  al_sensor_hal_err_t err = al_sensor_hal_read(&data);
  if (err != AL_SENSOR_HAL_OK) {
    log(AL_ULP_TYPE_ERROR, err);
    return;
  }

  // store reading
  readings[num_readings] = data;

  // increment
  num_readings++;

  // stop if full
  if (num_readings >= READINGS) {
    ulp_riscv_timer_stop();
    ulp_riscv_wakeup_main_processor();
  }
}

int main(void) {
  // mark run as in progress right away, so the main CPU observes a launched
  // run before it decides to halt the core during a handover
  running = 1;

  // wire sensor
  al_sensor_hal_init(
      (al_sensor_hal_ops_t){
          .transfer = al_ulp_transfer,
          .delay = delay,
          .epoch = epoch,
          .condition = true,
      },
      (al_sensor_hal_state_t *)&state);

  // handle a requested handover: turn off an active heater, then acknowledge
  // and self-halt, so the main CPU can take over a heater-less sensor
  if (handover) {
    if (state.heat != 0) {
      al_sensor_hal_err_t err = al_sensor_hal_heater_off();
      if (err == AL_SENSOR_HAL_OK) {
        state.heat = 0;
      } else {
        log(AL_ULP_TYPE_ERROR, err);
      }
    }
    ulp_riscv_timer_stop();
    ack = 1;
    running = 0;
    return 0;
  }

  // perform run
  run();

  // clear marker
  running = 0;

  return 0;
}
