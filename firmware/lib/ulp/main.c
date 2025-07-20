#include <ulp_riscv.h>
#include <ulp_riscv_utils.h>
#include <ulp_riscv_i2c.h>

#include "../sensor_hal.h"

#define READINGS 16  // 80s
#define DEBUG false

// use our own constant to avoid software floating point calculations
#define MS_CYCLES 17500
static_assert(MS_CYCLES == ULP_RISCV_CYCLES_PER_MS, "cycles mismatch");

static al_sensor_hal_data_t data = {0};

volatile int64_t start = 0;
volatile uint8_t counter = 0;
volatile al_sensor_hal_data_t readings[READINGS] = {0};

static bool transfer(uint8_t addr, uint8_t* wd, size_t wl, uint8_t* rd, size_t rl) {
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

  return true;
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

  // read sensor
  for (;;) {
    // wait
    delay(100);

    // check if ready
    if (al_sensor_hal_ready() != AL_SENSOR_HAL_OK) {
      continue;
    }

    // read sensor
    if (al_sensor_hal_read(&data) != AL_SENSOR_HAL_OK) {
      continue;
    }

    // store reading
    readings[counter] = data;

    // increment
    counter++;

    // stop if full
    if (counter >= READINGS) {
      break;
    }
  }

  // wake main CPU
  ulp_riscv_wakeup_main_processor();

  return 0;
}
