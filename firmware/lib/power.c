#include <naos.h>
#include <naos/sys.h>
#include <driver/adc.h>
#include <driver/rtc_io.h>
#include <esp_adc_cal.h>
#include <art32/numbers.h>
#include <math.h>

#include <al/core.h>
#include <al/power.h>

#include "internal.h"

// Chips: BQ25601, STM6601GU2BDM6F

#define AL_POWER_ADDR 0x6B
#define AL_POWER_HOLD GPIO_NUM_21
#define AL_POWER_LOW GPIO_NUM_14
#define AL_POWER_USB_CC1 ADC1_CHANNEL_5  // IO6
#define AL_POWER_USB_CC2 ADC1_CHANNEL_6  // IO7
#define AL_POWER_BAT_LVL ADC1_CHANNEL_7  // IO8
#define AL_POWER_SAMPLES 3
#define AL_POWER_CHG_TMP_MIN 10.f
#define AL_POWER_CHG_TMP_MAX 45.f
#define AL_POWER_CHG_TMP_HYST 2.f
#define AL_POWER_DEBUG false

typedef struct {
  int samples[AL_POWER_SAMPLES];
  int index;
} al_power_adc_t;

static naos_mutex_t al_power_mutex;
static al_power_hook_t al_power_hook;
static esp_adc_cal_characteristics_t al_power_calib;
static al_power_state_t al_power_state = {0};
static al_power_adc_t al_power_adc_cc1 = {0};
static al_power_adc_t al_power_adc_cc2 = {0};
static al_power_adc_t al_power_adc_bat = {0};

static struct {
  // config
  union {
    struct {
      uint8_t iindpm : 5;
      uint8_t _omitted : 3;
    };
    uint8_t raw;
  } reg0;
  union {
    struct {
      uint8_t _omitted1 : 4;
      uint8_t chg_config : 1;
      uint8_t _omitted2 : 1;
      uint8_t wdt_rst : 1;
      uint8_t _omitted3 : 1;
    };
    uint8_t raw;
  } reg1;
  union {
    struct {
      uint8_t ichg : 6;
      uint8_t _omitted : 2;
    };
    uint8_t raw;
  } reg2;
  union {
    struct {
      uint8_t _omitted1 : 4;
      uint8_t watchdog : 2;
      uint8_t _omitted2 : 2;
    };
    uint8_t raw;
  } reg5;
  // status
  union {
    struct {
      uint8_t vsys_stat : 1;
      uint8_t therm_stat : 1;
      uint8_t pg_stat : 1;
      uint8_t chrg_stat : 2;
      uint8_t vbus_stat : 3;
    };
    uint8_t raw;
  } reg8;
  union {
    struct {
      uint8_t ntc_fault : 3;
      uint8_t bat_fault : 1;
      uint8_t chrg_fault : 2;
      uint8_t boost_fault : 1;
      uint8_t wd_fault : 1;
    };
    uint8_t raw;
  } reg9;
  union {
    struct {
      uint8_t int_mask : 2;
      uint8_t acov_stat : 1;
      uint8_t topoff_active : 1;
      uint8_t _reserved : 1;
      uint8_t iindpm_stat : 1;
      uint8_t vindpm_stat : 1;
      uint8_t vbus_gd : 1;
    };
    uint8_t raw;
  } regA;
} al_power_memory;

static bool al_power_read(uint8_t reg, uint8_t *buf, size_t len) {
  // read data
  esp_err_t err = al_i2c_transfer(AL_POWER_ADDR, &reg, 1, buf, len, 1000);
  ESP_ERROR_CHECK_WITHOUT_ABORT(err);

  return err == ESP_OK;
}

static void al_power_write(uint8_t reg, uint8_t val, bool may_fail) {
  // write data
  uint8_t data[2] = {reg, val};
  esp_err_t err = al_i2c_transfer(AL_POWER_ADDR, data, 2, NULL, 0, 1000);
  if (may_fail) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);
  } else {
    ESP_ERROR_CHECK(err);
  }
}

static int al_power_adc(al_power_adc_t *adc, adc_channel_t channel) {
  // read ADC value
  int raw = (int)esp_adc_cal_raw_to_voltage(adc1_get_raw(channel), &al_power_calib);

  // update samples
  adc->samples[adc->index] = raw;
  adc->index = (adc->index + 1) % AL_POWER_SAMPLES;

  // find max
  int max = 0;
  for (int i = 0; i < AL_POWER_SAMPLES; i++) {
    max = adc->samples[i] > max ? adc->samples[i] : max;
  }

  return max;
}

void al_power_check() {
  // acquire mutex
  naos_lock(al_power_mutex);

  // read inputs
  bool low = gpio_get_level(AL_POWER_LOW) == 0;
  int cc1 = al_power_adc(&al_power_adc_cc1, AL_POWER_USB_CC1);
  int cc2 = al_power_adc(&al_power_adc_cc2, AL_POWER_USB_CC2);
  int bat = al_power_adc(&al_power_adc_bat, AL_POWER_BAT_LVL) * 2;
  if (AL_POWER_DEBUG) {
    naos_log("al-pwr: inputs low=%d cc1=%dmV cc2=%dmV bat=%dmV", low, cc1, cc2, bat);
  }

  // determine conditions
  bool has_usb = cc1 > 10 || cc2 > 10;
  bool can_fast = cc1 > 700 || cc2 > 700;  // 1.5A

  // read config
  if (!al_power_read(0x00, &al_power_memory.reg0.raw, 3)) {
    naos_unlock(al_power_mutex);
    return;
  }
  bool fast_charge = al_power_memory.reg0.iindpm > 0x4;  // 500mA
  if (AL_POWER_DEBUG) {
    naos_log("al-pwr: config fast_charge=%d iindpm=%d ichg=%d", fast_charge, al_power_memory.reg0.iindpm,
             al_power_memory.reg2.ichg);
  }

  // read status
  if (!al_power_read(0x08, &al_power_memory.reg8.raw, 3)) {
    naos_unlock(al_power_mutex);
    return;
  }
  bool charging = al_power_memory.reg8.chrg_stat != 0;
  bool power_good = al_power_memory.reg8.pg_stat == 1;
  bool any_fault = al_power_memory.reg9.raw != 0;
  bool usb_pwr = al_power_memory.regA.vbus_gd == 1;
  if (AL_POWER_DEBUG) {
    naos_log("al-pwr: status charging=%d power_good=%d any_fault=%d usb_pwr=%d", charging, power_good, any_fault,
             usb_pwr);
    if (any_fault) {
      naos_log("al-pwr: faults ntc=%d bat=%d chrg=%d boost=%d wd=%d", al_power_memory.reg9.ntc_fault,
               al_power_memory.reg9.bat_fault, al_power_memory.reg9.chrg_fault, al_power_memory.reg9.boost_fault,
               al_power_memory.reg9.wd_fault);
    }
  }

  // get ambient temperature
  float tmp = al_sensor_raw_temp();

  // determine if should be charging using hysteresis
  static bool should_charge = true;
  if (!isnan(tmp)) {
    if (tmp < AL_POWER_CHG_TMP_MIN || tmp > AL_POWER_CHG_TMP_MAX) {
      should_charge = false;
    } else if (tmp > AL_POWER_CHG_TMP_MIN + AL_POWER_CHG_TMP_HYST &&
               tmp < AL_POWER_CHG_TMP_MAX - AL_POWER_CHG_TMP_HYST) {
      should_charge = true;
    }
  }

  /* apply changes */

  // configure charging
  if (should_charge != (al_power_memory.reg1.chg_config == 1)) {
    al_power_memory.reg1.chg_config = should_charge ? 1 : 0;
    al_power_write(0x01, al_power_memory.reg1.raw, true);
    if (AL_POWER_DEBUG) {
      naos_log("al-pwr: charger %s", should_charge ? "enabled" : "disabled");
    }
  }

  // update max current setting to 900mA
  if (charging && can_fast != fast_charge) {
    al_power_memory.reg0.iindpm = can_fast ? 0x8 : 0x4;
    al_power_write(0x00, al_power_memory.reg0.raw, true);
    if (AL_POWER_DEBUG) {
      naos_log("al-pwr: input current limit set to %dmA", can_fast ? 900 : 500);
    }
  }

  // reset watchdog
  al_power_memory.reg1.wdt_rst = 1;
  al_power_write(0x01, al_power_memory.reg1.raw, true);

  /* update state */

  // prepare state
  al_power_state_t state = {
      .bat_voltage = (float)bat / 1000.f,
      .bat_level = a32_safe_map_f((float)bat, 3200.f, 4000.f, 0.f, 1.f),
      .has_usb = has_usb,
      .can_fast = can_fast,
      .charging = charging,
  };
  if (AL_POWER_DEBUG) {
    naos_log("al-pwr: state bat_level=%f has_usb=%d can_fast=%d", state.bat_level, state.has_usb, state.can_fast);
  }

  // determine if state changed
  bool changed = state.has_usb != al_power_state.has_usb || state.charging != al_power_state.charging;

  // update state
  al_power_state = state;

  // release mutex
  naos_unlock(al_power_mutex);

  // dispatch state if changed
  if (changed && al_power_hook != NULL) {
    al_power_hook(state);
  }
}

void al_power_init() {
  // create mutex
  al_power_mutex = naos_mutex();

  // hold power
  ESP_ERROR_CHECK(rtc_gpio_init(AL_POWER_HOLD));
  ESP_ERROR_CHECK(rtc_gpio_set_direction(AL_POWER_HOLD, RTC_GPIO_MODE_OUTPUT_ONLY));
  ESP_ERROR_CHECK(rtc_gpio_set_level(AL_POWER_HOLD, 1));
  ESP_ERROR_CHECK(rtc_gpio_hold_en(AL_POWER_HOLD));

  // low power
  gpio_config_t cfg = (gpio_config_t){
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = BIT64(AL_POWER_LOW),
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));

  // configure ADC (4096 = ~2.45V)
  ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));
  ESP_ERROR_CHECK(adc1_config_channel_atten(AL_POWER_USB_CC1, ADC_ATTEN_DB_12));
  ESP_ERROR_CHECK(adc1_config_channel_atten(AL_POWER_USB_CC2, ADC_ATTEN_DB_12));
  ESP_ERROR_CHECK(adc1_config_channel_atten(AL_POWER_BAT_LVL, ADC_ATTEN_DB_12));
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &al_power_calib);

  // increase watchdog timeout
  al_power_read(0x05, &al_power_memory.reg5.raw, 1);
  al_power_memory.reg5.watchdog = 0b10;  // 80 seconds
  al_power_write(0x05, al_power_memory.reg5.raw, false);

  // mask interrupts
  al_power_write(0x0A, 0b11, false);

  // check power
  al_power_check();

  // run check
  naos_repeat("al-pwr", 1000, al_power_check);
}

void al_power_config(al_power_hook_t hook) {
  // set hook
  naos_lock(al_power_mutex);
  al_power_hook = hook;
  naos_unlock(al_power_mutex);
}

al_power_state_t al_power_get() {
  // capture state
  naos_lock(al_power_mutex);
  al_power_state_t state = al_power_state;
  naos_unlock(al_power_mutex);

  return state;
}

void al_power_off() {
  // power down
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_hold_dis(AL_POWER_HOLD));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_set_level(AL_POWER_HOLD, 0));

  // delay
  naos_delay(2000);

  /* power off did not work */

  // power up
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_set_level(AL_POWER_HOLD, 1));
  ESP_ERROR_CHECK_WITHOUT_ABORT(rtc_gpio_hold_en(AL_POWER_HOLD));

  // go to deep sleep
  al_sleep(true, 0);
}

void al_power_ship() {
  // read settings
  uint8_t settings;
  if (!al_power_read(0x07, &settings, 1)) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // set ship mode without delay
  settings |= 0x20;
  settings &= ~0x8;

  // write settings
  al_power_write(0x07, settings, false);

  // delay
  naos_delay(2000);

  /* ship mode did not work */

  // clear ship mode
  settings &= ~0x20;

  // write settings
  al_power_write(0x07, settings, false);
}
