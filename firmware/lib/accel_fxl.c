#include <naos.h>
#include <naos/sys.h>

#include <al/core.h>

#include "accel.h"

// Chip: FXLS8974CF

#define AL_ACCEL_FXL_ADDR 0x18
#define AL_ACCEL_FXL_DEBUG false

// INT_PIN_SEL: push-pull, active-low, wake from sleep
#define AL_ACCEL_FXL_INT_CFG 0b00010010

static uint16_t al_accel_fxl_rot_map[] = {180, 0, 90, 270};

static void al_accel_fxl_write(uint8_t reg, uint8_t val) {
  // write data
  uint8_t data[2] = {reg, val};
  ESP_ERROR_CHECK(al_i2c_transfer(AL_ACCEL_FXL_ADDR, data, 2, NULL, 0, 1000));
}

static bool al_accel_fxl_read(uint8_t reg, uint8_t *val) {
  // read data
  esp_err_t err = al_i2c_transfer(AL_ACCEL_FXL_ADDR, &reg, 1, val, 1, 1000);
  ESP_ERROR_CHECK_WITHOUT_ABORT(err);

  return err == ESP_OK;
}

static bool al_accel_fxl_probe(uint8_t reg, uint8_t *val) {
  // read data without logging errors
  return al_i2c_transfer(AL_ACCEL_FXL_ADDR, &reg, 1, val, 1, 1000) == ESP_OK;
}

bool al_accel_fxl_detect() {
  // check WHO_AM_I
  uint8_t who = 0;
  return al_accel_fxl_probe(0x13, &who) && who == 0x86;
}

void al_accel_fxl_init(bool reset) {
  // force reset if interrupt config is incorrect
  uint8_t int_cfg = 0;
  bool ok = al_accel_fxl_read(0x18, &int_cfg);
  if (!ok || int_cfg != AL_ACCEL_FXL_INT_CFG) {
    naos_log("al-acc: forcing reset: int_cfg=%d ok=%d", int_cfg, ok);
    reset = true;
  }

  // perform reset
  if (reset) {
    // reset device
    al_accel_fxl_write(0x15, 0b10000000);

    // wait for reset to complete
    naos_delay(5);  // 1ms per datasheet

    // configure interrupt driver, polarity and wake from sleep
    al_accel_fxl_write(0x18, AL_ACCEL_FXL_INT_CFG);

    // enable orientation interrupt
    al_accel_fxl_write(0x20, 0b00001000);

    // enable orientation detection with debounce
    al_accel_fxl_write(0x29, 0b01000000);
    al_accel_fxl_write(0x2A, 6);

    // set ODR to 6.25Hz
    al_accel_fxl_write(0x17, 0b10011001);

    // activate device
    al_accel_fxl_write(0x15, 0b00000001);
  }
}

void al_accel_fxl_quiesce() {
  // skip if already quiet: interrupt pin configured to idle high and all
  // interrupts disabled
  uint8_t int_cfg = 0, int_en = 0;
  bool ok = al_accel_fxl_probe(0x18, &int_cfg) && al_accel_fxl_probe(0x20, &int_en);
  if (ok && int_cfg == AL_ACCEL_FXL_INT_CFG && int_en == 0) {
    return;
  }

  // reset device and configure the interrupt pin to idle high, keeping all
  // interrupts disabled
  al_accel_fxl_write(0x15, 0b10000000);
  naos_delay(5);
  al_accel_fxl_write(0x18, AL_ACCEL_FXL_INT_CFG);
  al_accel_fxl_write(0x17, 0b10011001);  // ODR 6.25Hz
  al_accel_fxl_write(0x15, 0b00000001);  // activate
}

bool al_accel_fxl_check(al_accel_state_t *state) {
  // read orientation
  uint8_t orientation = 0;
  if (!al_accel_fxl_read(0x28, &orientation)) {
    return false;
  }

  // check orientation
  bool front = orientation & 0b1;
  uint16_t rot = al_accel_fxl_rot_map[(orientation >> 1) & 0b11];
  bool lock = orientation & 0b1000000;
  if (AL_ACCEL_FXL_DEBUG) {
    naos_log("al-acc: front=%d rot=%d lock=%d", front, rot, lock);
  }

  // prepare state
  state->front = front;
  state->rotation = rot;

  return true;
}
