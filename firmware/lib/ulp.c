#include <naos.h>
#include <string.h>
#include <naos/sys.h>
#include <ulp_riscv.h>
#include <ulp_riscv_i2c.h>
#include <esp_sleep.h>
#include <hal/rtc_cntl_ll.h>

#include <al/clock.h>

#include "internal.h"
#include "sensor_hal.h"

#include "ulp_al.h"
#include "ulp/shared.h"

extern const uint8_t al_ulp_bin_start[] asm("_binary_ulp_al_bin_start");
extern const uint8_t al_ulp_bin_end[] asm("_binary_ulp_al_bin_end");

void al_ulp_stop() {
  // halt ULP program directly if not woken from deep sleep, as in this case the
  // ULP program was never started and the handover flags and state are stale
  if (!al_wakeup_cause()) {
    ulp_riscv_timer_stop();
    ulp_riscv_halt();
    return;
  }

  // otherwise, request a handover: new runs will turn off an active heater,
  // acknowledge and self-halt without touching the sensors otherwise
  ulp_handover = 1;

  // wait until the ULP program is idle with an inactive heater, or has
  // acknowledged the handover after cleaning up
  al_sensor_hal_state_t *state = (al_sensor_hal_state_t *)&ulp_state;
  for (int i = 0; i < 250; i++) {
    if (ulp_ack || (!ulp_running && state->heat == 0)) {
      break;
    }
    naos_delay(10);
  }

  // stop ULP program
  ulp_riscv_timer_stop();
  ulp_riscv_halt();

  // if the heater is still flagged active, the handover timed out or the ULP
  // failed to turn it off, therefore we force it off through the sensor HAL
  // using the still configured RTC I2C peripheral (the HAL is re-initialized
  // with the main CPU ops later during sensor initialization)
  if (state->heat != 0) {
    naos_log("al-ulp: handover failed: forcing heater off");
    al_sensor_hal_init((al_sensor_hal_ops_t){.transfer = al_ulp_transfer}, state);
    al_sensor_hal_err_t err = al_sensor_hal_heater_off();
    if (err != AL_SENSOR_HAL_OK) {
      naos_log("al-ulp: HAL error=%d", err);
    }
    state->heat = 0;
  }
}

void al_ulp_init(bool reset) {
  // clear memory on reset to prevent access of uninitialized memory
  if (reset) {
    memset(ulp_offset, 0, sizeof(ulp_offset));
    memset(ulp_start, 0, sizeof(ulp_start));
    ulp_num_readings = 0;
    ulp_num_logs = 0;
  }

  // print logs
  for (int i = 0; i < ulp_num_logs; i++) {
    al_ulp_log_t log = ((al_ulp_log_t *)&ulp_logs)[i];
    if (log.type == AL_ULP_TYPE_ERROR) {
      naos_log("al-ulp: error: [%d] %lld", log.time, log.value);
    } else {
      naos_log("al-ulp: log/%d: [%d] %lld", log.type, log.time, log.value);
    }
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

  // clear handover flags
  ulp_handover = 0;
  ulp_running = 0;
  ulp_ack = 0;

  // start ULP program
  ESP_ERROR_CHECK(ulp_riscv_run());
  ulp_riscv_timer_resume();

  // get pointers
  al_sensor_hal_state_t *state = (al_sensor_hal_state_t *)&ulp_state;
  uint64_t *offset = (uint64_t *)&ulp_offset;
  int64_t *start = (int64_t *)&ulp_start;

  // set state
  *state = al_sensor_hal_dump();
  *offset = rtc_cntl_ll_get_rtc_time();
  *start = al_clock_get_epoch();

  // log
  naos_log("al-ulp: started: length=%d", al_ulp_bin_end - al_ulp_bin_start);
}

void al_ulp_sync() {
  // copy the sensor state to the ULP memory and drop stale readings, so a
  // wake up from a ULP-less deep sleep restores the actual state and does
  // not ingest readings from an earlier ULP run again
  al_sensor_hal_state_t *state = (al_sensor_hal_state_t *)&ulp_state;
  *state = al_sensor_hal_dump();
  ulp_num_readings = 0;
  ulp_num_logs = 0;
}

void al_ulp_load_state(al_sensor_hal_state_t *state) {
  // check if ULP woke up
  if (!al_wakeup_cause()) {
    return;
  }

  // copy state
  al_sensor_hal_state_t *s = (al_sensor_hal_state_t *)&ulp_state;
  *state = *s;
}

int al_ulp_readings() {
  // check if ULP woke up
  if (!al_wakeup_cause()) {
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
