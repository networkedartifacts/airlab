#include <math.h>
#include <naos.h>
#include <naos/sys.h>
#include <driver/gpio.h>

#include <al/touch.h>

#include "internal.h"

#define AL_TOUCH_ADDR 0x37
#define AL_TOUCH_INT GPIO_NUM_11
#define AL_TOUCH_DEBUG false
#define AL_TOUCH_DEBUG_SENSOR (-1)

static naos_mutex_t al_touch_mutex;
static uint8_t al_touch_map[8] = {2, 6, 1, 0, 5, 4, 3};
static uint8_t al_touch_last = 0;
static al_touch_hook_t al_touch_hook = NULL;

static void al_touch_read(uint8_t reg, uint8_t *buf, size_t len) {
  // read data
  for (size_t i = 0; i < 10; i++) {
    esp_err_t err = al_i2c_transfer(AL_TOUCH_ADDR, &reg, 1, buf, len, 1000);
    if (err != ESP_FAIL) {
      ESP_ERROR_CHECK(err);
      return;
    }
    if (AL_TOUCH_DEBUG) {
      naos_log("al-tch: retrying read...");
    }
    naos_delay(100);
  }
  ESP_ERROR_CHECK(ESP_FAIL);
}

static uint8_t al_touch_read8(uint8_t reg) {
  // read register
  uint8_t value;
  al_touch_read(reg, &value, 1);

  return value;
}

static uint16_t al_touch_read16(uint8_t reg) {
  // read register
  uint8_t data[2];
  al_touch_read(reg, data, 2);

  return data[0] << 8 | data[1];
}

static void al_touch_write(uint8_t reg, const uint8_t *buf, size_t len) {
  static uint8_t data[32];

  // copy data
  data[0] = reg;
  for (size_t i = 0; i < len; i++) {
    data[i + 1] = buf[i];
  }

  // read data
  ESP_ERROR_CHECK(al_i2c_transfer(AL_TOUCH_ADDR, data, 1 + len, NULL, 0, 1000));
}

static void al_touch_write8(uint8_t reg, uint8_t value) {
  // write register
  al_touch_write(reg, &value, 1);
}

static void al_touch_exec(uint8_t cmd) {
  // check command control
  uint8_t ctrl = al_touch_read8(0x86);
  if (ctrl != 0) {
    naos_log("al-tch: busy ctrl=%02x", ctrl);
    ESP_ERROR_CHECK(ESP_FAIL);
    return;
  }

  // execute command
  if (AL_TOUCH_DEBUG) {
    naos_log("al-tch: exec cmd=%02x", cmd);
  }
  al_touch_write8(0x86, cmd);

  // wait
  while (al_touch_read8(0x86) != 0) {
    if (AL_TOUCH_DEBUG) {
      naos_log("al-tch: wait...");
    }
    naos_delay(1);
  }
  if (AL_TOUCH_DEBUG) {
    naos_log("al-tch: done");
  }

  // check error
  uint8_t failed = al_touch_read8(0x88) & 0x01;
  uint8_t code = al_touch_read8(0x89);
  if (failed) {
    naos_log("al-tch: failed code=%02x", code);
    ESP_ERROR_CHECK(ESP_FAIL);
  }
}

static bool al_touch_valid(uint8_t num) {
  // check for exactly 0 or 1 bit set
  if (__builtin_popcount(num) <= 1) {
    return true;
  }

  // check for exactly 2 adjacent bits set
  return (num & (num >> 1)) && __builtin_popcount(num) == 2;
}

static float al_touch_middle(uint8_t num) {
  // prepare state
  int start = -1, end = -1;
  int cur_len = 0, max_len = 0;
  int tmp_start = -1;

  // find continuous bits
  for (int i = 0; i < 8; i++) {
    if (num & (1 << i)) {
      if (tmp_start == -1) {
        tmp_start = i;
      }
      cur_len++;
    } else {
      if (cur_len > max_len) {
        max_len = cur_len;
        start = tmp_start;
        end = i - 1;
      }
      tmp_start = -1;
      cur_len = 0;
    }
  }

  // handle last bit
  if (cur_len > max_len) {
    start = tmp_start;
    end = 8 - 1;
  }

  // check if no bits
  if (start == -1) {
    return -1;
  }

  return (float)(start + end) / 2.0f;
}

static float al_touch_hysteresis(float position) {
  // prepare state
  static float discrete = NAN;

  // if no touch, return NaN and reset state
  if (isnan(position)) {
    discrete = NAN;
    return NAN;
  }

  // if no previous discrete position, use current rounded position
  if (isnan(discrete)) {
    discrete = roundf(position);
    return discrete;
  }

  // apply hysteresis: require crossing threshold to change position
  float threshold = 0.7f;
  float diff = position - discrete;

  // move up or down one step if crossing threshold
  if (diff >= threshold) {
    discrete = discrete + 1.0f;
  } else if (diff <= -threshold) {
    discrete = discrete - 1.0f;
  }

  // clamp to valid range [-3, 3]
  if (discrete > 3.0f) {
    discrete = 3.0f;
  } else if (discrete < -3.0f) {
    discrete = -3.0f;
  }

  return discrete;
}

static void al_touch_check() {
  // lock mutex
  naos_lock(al_touch_mutex);

  // read touches
  uint8_t touches;
  al_touch_read(0xAA, &touches, 1);

  // read debug status
  if (AL_TOUCH_DEBUG && AL_TOUCH_DEBUG_SENSOR >= 0) {
    uint8_t cp = al_touch_read8(0xdd);     // pF
    uint16_t dc = al_touch_read16(0xde);   // difference count
    uint16_t bl = al_touch_read16(0xe0);   // baseline
    uint16_t rc = al_touch_read16(0xe2);   // raw count
    uint16_t arc = al_touch_read16(0xe4);  // average raw count
    naos_log("al-tch: debug pad=%d cp=%d dc=%d bl=%d rc=%d arc=%d", AL_TOUCH_DEBUG_SENSOR, cp, dc, bl, rc, arc);
  }

  // remap touches
  uint8_t remapped = 0;
  for (size_t i = 0; i < 7; i++) {
    if (touches & (1 << i)) {
      remapped |= (1 << al_touch_map[i]);
    }
  }
  touches = remapped;

  // ignore, if unchanged
  if (touches == al_touch_last) {
    naos_unlock(al_touch_mutex);
    return;
  }

  // log touches
  if (AL_TOUCH_DEBUG) {
    uint8_t t1 = touches & 0x01;
    uint8_t t2 = (touches >> 1) & 0x01;
    uint8_t t3 = (touches >> 2) & 0x01;
    uint8_t t4 = (touches >> 3) & 0x01;
    uint8_t t5 = (touches >> 4) & 0x01;
    uint8_t t6 = (touches >> 5) & 0x01;
    uint8_t t7 = (touches >> 6) & 0x01;
    naos_log("al-tch: touches %d %d %d %d %d %d %d", t1, t2, t3, t4, t5, t6, t7);
  }

  // try to clear stray bit 2 if 5/6 is set
  if ((touches & 0b1100000) != 0 && (touches & 0b100) != 0) {
    touches &= ~0b100;
    if (AL_TOUCH_DEBUG) {
      naos_log("al-tch: cleared stray bit 2");
    }
  }

  // ignore invalid touches
  if (!al_touch_valid(touches)) {
    if (AL_TOUCH_DEBUG) {
      naos_log("al-tch: invalid touches");
    }
    naos_unlock(al_touch_mutex);
    return;
  }

  // update last touches
  al_touch_last = touches;

  // calculate middle, if touched
  float middle = NAN;
  if (touches != 0) {
    middle = al_touch_middle(touches) - 3;
  }

  // apply hysteresis to get discrete position
  float position = al_touch_hysteresis(middle);
  if (AL_TOUCH_DEBUG) {
    naos_log("al-tch: middle=%.2f discrete=%.0f", middle, position);
  }

  // unlock mutex
  naos_unlock(al_touch_mutex);

  // dispatch event
  if (al_touch_hook != NULL) {
    al_touch_hook(position);
  }
}

static void al_touch_signal() {
  // defer check if low
  if (gpio_get_level(AL_TOUCH_INT) == 0) {
    naos_defer_isr(al_touch_check);
  }
}

static void al_touch_reset() {
  // await device
  al_touch_read8(0x86);

  // reset device
  al_touch_exec(255);

  // set sensitivity
  al_touch_write8(0x08, 0xFF);
  al_touch_write8(0x09, 0xFF);

  // increase thresholds
  al_touch_write8(0x0c, 196);
  al_touch_write8(0x0d, 196);
  al_touch_write8(0x0e, 196);
  al_touch_write8(0x0f, 196);
  al_touch_write8(0x10, 196);
  al_touch_write8(0x11, 196);
  al_touch_write8(0x12, 196);

  // configure CS7 as shield (SPO1) and HI as interrupt (SPO0)
  al_touch_write8(0x4c, 0b00100100);

  // enable shield
  al_touch_write8(0x4f, 0b00000001);

  // enable sensors
  al_touch_write8(0x00, 0b01111111);

  // calculate checksum
  al_touch_exec(3);

  // copy checksum
  uint8_t checksum[2];
  al_touch_read(0x94, checksum, 2);
  al_touch_write(0x7e, checksum, 2);

  // store configuration
  al_touch_exec(2);

  // reset device
  al_touch_exec(255);

  // set debug sensor
  if (AL_TOUCH_DEBUG && AL_TOUCH_DEBUG_SENSOR >= 0) {
    al_touch_write8(0x82, al_touch_map[AL_TOUCH_DEBUG_SENSOR]);
  }
}

void al_touch_init(bool reset) {
  // perform reset
  if (reset) {
    al_touch_reset();
  }

  // create mutex
  al_touch_mutex = naos_mutex();

  // setup interrupt
  gpio_config_t io_cfg = {
      .pin_bit_mask = BIT64(AL_TOUCH_INT),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };
  ESP_ERROR_CHECK(gpio_config(&io_cfg));
  ESP_ERROR_CHECK(gpio_isr_handler_add(AL_TOUCH_INT, al_touch_signal, NULL));

  // exit lower power device
  al_touch_read8(0x86);
}

void al_touch_config(al_touch_hook_t hook) {
  // lock mutex
  naos_lock(al_touch_mutex);

  // store hook
  al_touch_hook = hook;

  // unlock mutex
  naos_unlock(al_touch_mutex);
}

void al_touch_sleep() {
  // lock mutex
  naos_lock(al_touch_mutex);

  // enter low power mode
  al_touch_read8(0x86);
  al_touch_write8(0x86, 7);

  // unlock mutex
  naos_unlock(al_touch_mutex);
}

void al_touch_wake() {
  // lock mutex
  naos_lock(al_touch_mutex);

  // exit low power mode
  al_touch_read8(0x86);

  // unlock mutex
  naos_unlock(al_touch_mutex);
}
