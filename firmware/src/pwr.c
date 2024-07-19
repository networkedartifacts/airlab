#include <naos.h>
#include <naos/sys.h>
#include <driver/i2c.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <art32/numbers.h>
#include <esp_sleep.h>

#include "pwr.h"
#include "sig.h"

#define PWR_ADDR 0x68
#define PWR_HOLD GPIO_NUM_21
#define PWR_USB_CC1 ADC1_CHANNEL_5  // IO6
#define PWR_USB_CC2 ADC1_CHANNEL_6  // IO7
#define PWR_BAT_LVL ADC1_CHANNEL_7  // IO8
#define PWR_DEBUG false

static naos_mutex_t pwr_mutex;
static esp_adc_cal_characteristics_t pwr_calib;
static pwr_state_t pwr_state = {0};

static void pwr_read(uint8_t reg, uint8_t *buf, size_t len) {
  // read data
  ESP_ERROR_CHECK(i2c_master_write_read_device(I2C_NUM_0, PWR_ADDR, &reg, 1, buf, len, 1000));
}

void pwr_check() {
  // acquire mutex
  naos_lock(pwr_mutex);

  // read voltages
  int cc1 = (int)esp_adc_cal_raw_to_voltage(adc1_get_raw(PWR_USB_CC1), &pwr_calib);
  int cc2 = (int)esp_adc_cal_raw_to_voltage(adc1_get_raw(PWR_USB_CC2), &pwr_calib);
  int bat = (int)esp_adc_cal_raw_to_voltage(adc1_get_raw(PWR_BAT_LVL), &pwr_calib) * 2;
  if (PWR_DEBUG) {
    naos_log("bat: inputs cc1=%dmV cc2=%dmV bat=%dmV", cc1, cc2, bat);
  }

  bool charging = 0;  // TODO: Read from charger.
  bool charged = 0;   // TODO: Read from charger.
  if (PWR_DEBUG) {
    naos_log("pwr: charging/low=%d charged=%d", charging, charged);
  }

  // set state
  pwr_state.battery = a32_safe_map_f((float)bat, 3200.f, 4000.f, 0.f, 1.f);
  pwr_state.usb = cc1 || cc2;
  pwr_state.fast = (cc1 ? cc1 : cc2) > 350;
  pwr_state.charging = charging;
  if (PWR_DEBUG) {
    naos_log("pwr: battery=%f usb=%d fast=%d", pwr_state.battery, pwr_state.usb, pwr_state.fast);
  }

  // TODO: Adjust charging current.

  // release mutex
  naos_unlock(pwr_mutex);
}

void pwr_init() {
  // create mutex
  pwr_mutex = naos_mutex();

  // read status
  uint8_t status;
  pwr_read(0x08, &status, 1);
  naos_log("pwr: status=%02x", status);

  // configure ADC (4096 = ~2.45V)
  ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));
  ESP_ERROR_CHECK(adc1_config_channel_atten(PWR_USB_CC1, ADC_ATTEN_DB_12));
  ESP_ERROR_CHECK(adc1_config_channel_atten(PWR_USB_CC2, ADC_ATTEN_DB_12));
  ESP_ERROR_CHECK(adc1_config_channel_atten(PWR_BAT_LVL, ADC_ATTEN_DB_12));

  // characterize ADC
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &pwr_calib);

  // hold power
  gpio_config_t cfg = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = BIT64(PWR_HOLD),
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));
  ESP_ERROR_CHECK(gpio_set_level(PWR_HOLD, 1));

  // run check
  naos_repeat("pwr", 1000, pwr_check);
}

pwr_state_t pwr_get() {
  // acquire mutex
  naos_lock(pwr_mutex);

  // get state
  pwr_state_t state = pwr_state;

  // release mutex
  naos_unlock(pwr_mutex);

  return state;
}

void pwr_off() {
  // set pin
  gpio_set_level(PWR_HOLD, 0);  // off

  // delay
  naos_delay(2000);

  // fall back to deep sleep
  pwr_sleep(true, 0);
}

pwr_cause_t pwr_sleep(bool deep, uint64_t timeout) {
  // configure sleep hold
  ESP_ERROR_CHECK(gpio_hold_en(PWR_HOLD));
  gpio_deep_sleep_hold_en();

  // configure timeout
  if (timeout > 0) {
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(timeout * 1000));
  } else {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  }

  // perform sleep
  if (deep) {
    esp_deep_sleep_start();
  } else {
    ESP_ERROR_CHECK(esp_light_sleep_start());
  }

  // get cause
  pwr_cause_t cause = pwr_cause();

  // capture enter if unlocked
  if (cause == PWR_UNLOCK) {
    sig_await(SIG_ENTER, 1000);
  }

  return cause;
}

pwr_cause_t pwr_cause() {
  // get cause
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return PWR_TIMEOUT;
    case ESP_SLEEP_WAKEUP_EXT1:
      return PWR_UNLOCK;
    default:
      return PWR_NONE;
  }
}
