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

static al_sensor_hal_err_t transfer(uint8_t addr, uint8_t* wd, size_t wl, uint8_t* rd, size_t rl) {
  // set slave address
  ulp_riscv_i2c_master_set_slave_addr(addr);

  // write data
  if (wl > 0) {
    ulp_riscv_i2c_master_set_slave_reg_addr(wd[0]);
    if (wl > 1) {
      ulp_riscv_i2c_master_write_to_device(wd + 1, wl - 1);
    }
  }

  // read data
  if (rl > 0) {
    if (wl == 0 || wl > 1) {
      ulp_riscv_i2c_master_set_slave_reg_addr(0);
    }
    ulp_riscv_i2c_master_read_from_device(rd, rl);
  }

  return AL_SENSOR_HAL_OK;
}

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

int main(void) {
  // wire sensor
  al_sensor_hal_init(
      (al_sensor_hal_ops_t){
          .transfer = transfer,
          .delay = delay,
          .epoch = epoch,
      },
      (al_sensor_hal_state_t*)&state);

  // check if ready
  bool ready = al_sensor_hal_ready();
  if (!ready) {
    return 0;
  }

  // read sensor
  al_sensor_hal_err_t err = al_sensor_hal_read(&data);
  if (err != AL_SENSOR_HAL_OK) {
    log(AL_ULP_TYPE_ERROR, err);
    return 0;
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

  return 0;
}
