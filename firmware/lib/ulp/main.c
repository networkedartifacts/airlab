#include <ulp_riscv.h>
#include <ulp_riscv_utils.h>
#include <ulp_riscv_i2c.h>

#include "../sensor_hal.h"

#define READINGS 16  // 80s
#define ERRORS 64
#define DEBUG false

// use our own constant to avoid software floating point calculations
#define MS_CYCLES 17500
static_assert(MS_CYCLES == ULP_RISCV_CYCLES_PER_MS, "cycles mismatch");

static al_sensor_hal_data_t data = {0};

volatile int64_t start = 0;
volatile al_sensor_hal_data_t readings[READINGS] = {0};
volatile int num_readings = 0;
volatile int errors[ERRORS];
volatile int num_errors = 0;

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

static int64_t epoch() {
  // calculate milliseconds from cycles
  int millis = ULP_RISCV_GET_CCOUNT() / MS_CYCLES;

  return start + (int64_t)millis;
}

int main(void) {
  // wire sensor
  al_sensor_hal_wire((al_sensor_hal_ops_t){
      .transfer = transfer,
      .delay = delay,
      .epoch = epoch,
  });

  // check if ready
  al_sensor_hal_err_t err = al_sensor_hal_ready();
  if (err != AL_SENSOR_HAL_OK) {
    // log error
    if (err != AL_SENSOR_HAL_BUSY && num_errors < ERRORS) {
      errors[num_errors++] = err;
    }

    return 0;
  }

  // read sensor
  err = al_sensor_hal_read(&data);
  if (err != AL_SENSOR_HAL_OK) {
    // log error
    if (num_errors < ERRORS) {
      errors[num_errors++] = err;
    }

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
