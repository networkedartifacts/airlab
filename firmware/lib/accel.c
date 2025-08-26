#include <naos.h>
#include <naos/sys.h>
#include <driver/gpio.h>

#include <al/accel.h>

#include "internal.h"

// Chip: FXLS8974CF

#define AL_ACCEL_ADDR 0x18
#define AL_ACCEL_DEBUG false

static naos_mutex_t al_accel_mutex;
static al_accel_state_t al_accel_state = {0};
static uint16_t al_accel_rot_map[] = {180, 0, 90, 270};
static al_accel_hook_t al_accel_hook = NULL;

static void al_accel_write(uint8_t reg, uint8_t val) {
  // write data
  uint8_t data[2] = {reg, val};
  ESP_ERROR_CHECK(al_i2c_transfer(AL_ACCEL_ADDR, data, 2, NULL, 0, 1000));
}

static bool al_accel_read(uint8_t reg, uint8_t *val) {
  // read data
  esp_err_t err = al_i2c_transfer(AL_ACCEL_ADDR, &reg, 1, val, 1, 1000);
  ESP_ERROR_CHECK_WITHOUT_ABORT(err);

  return err == ESP_OK;
}

void al_accel_init(bool reset) {
  // perform reset
  if (reset) {
    // reset device
    al_accel_write(0x15, 0b10000000);

    // configure interrupt driver, polarity and wake from sleep
    al_accel_write(0x18, 0b00010010);

    // enable orientation interrupt
    al_accel_write(0x20, 0b00001000);

    // enable orientation detection with debounce
    al_accel_write(0x29, 0b01000000);
    al_accel_write(0x2A, 6);

    // set ODR to 6.25Hz
    al_accel_write(0x17, 0b10011001);

    // activate device
    al_accel_write(0x15, 0b00000001);
  }

  // create mutex
  al_accel_mutex = naos_mutex();

  // check immediately to clear interrupt
  al_accel_check();

  // run check task to ensure the interrupt is cleared eventually
  naos_repeat("al-acc", 1000, al_accel_check);
}

void al_accel_check() {
  // lock mutex
  naos_lock(al_accel_mutex);

  // read orientation
  uint8_t orientation = 0;
  if (!al_accel_read(0x28, &orientation)) {
    naos_unlock(al_accel_mutex);
    return;
  }

  // check orientation
  bool front = orientation & 0b1;
  uint16_t rot = al_accel_rot_map[(orientation >> 1) & 0b11];
  bool lock = orientation & 0b1000000;
  if (AL_ACCEL_DEBUG) {
    naos_log("al-acc: front=%d rot=%d lock=%d", front, rot, lock);
  }

  // prepare state
  al_accel_state_t state = {
      .front = front,
      .rotation = rot,
  };

  // determine if state changed
  bool changed = state.front != al_accel_state.front || state.rotation != al_accel_state.rotation;

  // update state
  al_accel_state = state;

  // unlock mutex
  naos_unlock(al_accel_mutex);

  // dispatch state if changed
  if (changed && al_accel_hook != NULL) {
    al_accel_hook(state);
  }
}

void al_accel_config(al_accel_hook_t hook) {
  // store hook
  naos_lock(al_accel_mutex);
  al_accel_hook = hook;
  naos_unlock(al_accel_mutex);
}

al_accel_state_t al_accel_get() {
  // capture state
  naos_lock(al_accel_mutex);
  al_accel_state_t state = al_accel_state;
  naos_unlock(al_accel_mutex);

  return state;
}
