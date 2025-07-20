#include <naos.h>
#include <ulp_riscv.h>
#include <ulp_riscv_i2c.h>
#include <esp_sleep.h>

#include <al/clock.h>

#include "internal.h"
#include "sensor_hal.h"

#include "ulp_al.h"

extern const uint8_t al_ulp_bin_start[] asm("_binary_ulp_al_bin_start");
extern const uint8_t al_ulp_bin_end[] asm("_binary_ulp_al_bin_end");

void al_ulp_stop() {
  // stop ULP program
  ulp_riscv_timer_stop();
  ulp_riscv_halt();
}

void al_ulp_init(bool reset) {
  // clear memory on reset to prevent access of uninitialized memory
  if (reset) {
    ulp_start = 0;
    ulp_num_readings = 0;
    ulp_num_errors = 0;
  }

  // print errors
  for (int i = 0; i < ulp_num_errors; i++) {
    int log = ((int *)&ulp_errors)[i];
    naos_log("al-ulp: error: %#010x", log);
  }
}

void al_ulp_start() {
  // load ULP program
  ESP_ERROR_CHECK(ulp_riscv_load_binary(al_ulp_bin_start, al_ulp_bin_end - al_ulp_bin_start));

  // configure ULP wake period
  ESP_ERROR_CHECK(ulp_set_wakeup_period(0, 1000 * 1000));

  // configure ULP I2C
  ulp_riscv_i2c_cfg_t i2c = ULP_RISCV_I2C_DEFAULT_CONFIG();
  i2c.i2c_pin_cfg.scl_io_num = GPIO_NUM_2;
  i2c.i2c_pin_cfg.sda_io_num = GPIO_NUM_1;
  ESP_ERROR_CHECK(ulp_riscv_i2c_master_init(&i2c));

  // prevent power down of I2C peripheral during ULP sleep
  ESP_ERROR_CHECK(esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON));

  // clear counters
  ulp_num_readings = 0;
  ulp_num_errors = 0;

  // start ULP program
  ESP_ERROR_CHECK(ulp_riscv_run());
  ulp_riscv_timer_resume();

  // store epoch
  int64_t *start = (int64_t *)&ulp_start;
  *start = al_clock_get_epoch();

  // log
  naos_log("al-ulp: started: length=%d", al_ulp_bin_end - al_ulp_bin_start);
}

int al_ulp_readings() {
  // check if ULP woke up
  if (!esp_sleep_get_wakeup_cause()) {
    return 0;
  }

  // read counter
  return (int)ulp_num_readings;
}

al_sensor_hal_data_t al_ulp_get_reading(int index) {
  // return zero if no readings
  if (ulp_num_readings == 0) {
    return (al_sensor_hal_data_t){0};
  }

  // set last reading on under/overflow
  if (index < 0 || index >= (int)ulp_num_readings) {
    index = (int)ulp_num_readings - 1;
  }

  return ((al_sensor_hal_data_t *)&ulp_readings)[index];
}
