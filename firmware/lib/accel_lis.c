#include <naos.h>
#include <naos/sys.h>

#include <al/core.h>

#include "accel.h"

// Chip: LIS2DH12

#define AL_ACCEL_LIS_ADDR 0x19
#define AL_ACCEL_LIS_DEBUG false

// CTRL3: IA1 interrupt routed to INT1
#define AL_ACCEL_LIS_CTRL3 0b01000000

static void al_accel_lis_write(uint8_t reg, uint8_t val) {
  // write data
  uint8_t data[2] = {reg, val};
  ESP_ERROR_CHECK(al_i2c_transfer(AL_ACCEL_LIS_ADDR, data, 2, NULL, 0, 1000, true));
}

static bool al_accel_lis_read(uint8_t reg, uint8_t *val) {
  // read data
  esp_err_t err = al_i2c_transfer(AL_ACCEL_LIS_ADDR, &reg, 1, val, 1, 1000, true);
  ESP_ERROR_CHECK_WITHOUT_ABORT(err);

  return err == ESP_OK;
}

static bool al_accel_lis_probe(uint8_t reg, uint8_t *val) {
  // read data without logging errors
  return al_i2c_transfer(AL_ACCEL_LIS_ADDR, &reg, 1, val, 1, 1000, false) == ESP_OK;
}

bool al_accel_lis_detect() {
  // check WHO_AM_I
  uint8_t who = 0;
  return al_accel_lis_probe(0x0F, &who) && who == 0x33;
}

void al_accel_lis_init(bool reset) {
  // force reset if interrupt config is incorrect
  uint8_t ctrl3 = 0;
  bool ok = al_accel_lis_read(0x22, &ctrl3);
  if (!ok || ctrl3 != AL_ACCEL_LIS_CTRL3) {
    naos_log("al-acc: forcing reset: ctrl3=%d ok=%d", ctrl3, ok);
    reset = true;
  }

  // perform reset
  if (reset) {
    // reboot memory content
    al_accel_lis_write(0x24, 0b10000000);

    // wait for reboot to complete (datasheet: 5ms)
    naos_delay(10);

    // configure ODR=10Hz, low-power mode, enable X/Y/Z
    al_accel_lis_write(0x20, 0b00101111);

    // disable high-pass filter
    al_accel_lis_write(0x21, 0b00000000);

    // set BDU, ±2g, low-power compatible
    al_accel_lis_write(0x23, 0b10000000);

    // latch interrupt on INT1
    al_accel_lis_write(0x24, 0b00001000);

    // active-low interrupt polarity
    al_accel_lis_write(0x25, 0b00000010);

    // set interrupt threshold (16mg/LSB at ±2g, low-power) → ~512mg
    al_accel_lis_write(0x32, 0x20);

    // set interrupt debounce (3 samples @ 10Hz → 300ms)
    al_accel_lis_write(0x33, 0x03);

    // enable 6D movement recognition on all axes
    al_accel_lis_write(0x30, 0b01111111);

    // route IA1 to INT1
    al_accel_lis_write(0x22, AL_ACCEL_LIS_CTRL3);
  }
}

bool al_accel_lis_check(al_accel_state_t *state) {
  // read interrupt source (clears latched interrupt)
  uint8_t src = 0;
  if (!al_accel_lis_read(0x31, &src)) {
    return false;
  }

  // extract interrupt-active and position bits
  bool ia = src & 0b01000000;
  bool zh = src & 0b00100000;
  bool zl = src & 0b00010000;
  bool yh = src & 0b00001000;
  bool yl = src & 0b00000100;
  bool xh = src & 0b00000010;
  bool xl = src & 0b00000001;
  if (AL_ACCEL_LIS_DEBUG) {
    naos_log("al-acc: ia=%d src=0x%02x zh=%d zl=%d yh=%d yl=%d xh=%d xl=%d", ia, src, zh, zl, yh, yl, xh, xl);
  }

  // determine face orientation, keeping stable values across spurious reads
  if (zh) {
    state->front = true;
  } else if (zl) {
    state->front = false;
  }

  // determine rotation from horizontal axes
  if (yh) {
    state->rotation = 0;
  } else if (yl) {
    state->rotation = 180;
  } else if (xh) {
    state->rotation = 90;
  } else if (xl) {
    state->rotation = 270;
  }

  return true;
}
