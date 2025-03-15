#include <naos.h>
#include <ulp_riscv.h>
#include <ulp_riscv_i2c.h>
#include <esp_sleep.h>

#include "internal.h"
#include "sensor_hal.h"

#include "ulp_al.h"

extern const uint8_t ulp_bin_start[] asm("_binary_ulp_al_bin_start");
extern const uint8_t ulp_bin_end[] asm("_binary_ulp_al_bin_end");

void al_ulp_start() {
  // load ULP program
  naos_log("al-ulp: length: %d", ulp_bin_end - ulp_bin_start);
  ESP_ERROR_CHECK(ulp_riscv_load_binary(ulp_bin_start, ulp_bin_end - ulp_bin_start));

  // configure ULP wake period
  ESP_ERROR_CHECK(ulp_set_wakeup_period(0, 1000 * 1000));

  // configure ULP I2C
  ulp_riscv_i2c_cfg_t i2c = ULP_RISCV_I2C_DEFAULT_CONFIG();
  i2c.i2c_pin_cfg.scl_io_num = GPIO_NUM_2;
  i2c.i2c_pin_cfg.sda_io_num = GPIO_NUM_1;
  ESP_ERROR_CHECK(ulp_riscv_i2c_master_init(&i2c));

  // clear counter
  ulp_counter = 0;

  // start ULP program
  ESP_ERROR_CHECK(ulp_riscv_run());
  ulp_riscv_timer_resume();
}

void al_ulp_stop() {
  // stop ULP program
  ulp_riscv_timer_stop();
  ulp_riscv_halt();
}

int al_ulp_readings() {
  // check if ULP woke up
  if (!esp_sleep_get_wakeup_cause()) {
    return 0;
  }

  // read counter
  return (int)ulp_counter;
}

al_sensor_raw_t al_ulp_get_reading(int index) {
  // return zero if no readings
  if (ulp_counter == 0) {
    return (al_sensor_raw_t){0};
  }

  // set last reading on under/overflow
  if (index < 0 || index >= (int)ulp_counter) {
    index = (int)ulp_counter - 1;
  }

  return ((al_sensor_raw_t*)&ulp_readings)[index];
}
