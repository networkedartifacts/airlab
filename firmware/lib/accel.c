#include <naos.h>
#include <naos/sys.h>
#include <driver/i2c.h>

#include <al/accel.h>

#include "internal.h"

#define AL_ACCEL_ADDR 0x18
#define AL_ACCEL_DEBUG false

static al_accel_state_t al_accel_state = {0};
static uint16_t al_accel_rot_map[] = {180, 0, 90, 270};

static void al_accel_write(uint8_t reg, uint8_t val) {
  // write data
  uint8_t data[2] = {reg, val};
  ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, AL_ACCEL_ADDR, data, 2, 1000));
}

static uint8_t al_accel_read(uint8_t reg) {
  uint8_t val;
  ESP_ERROR_CHECK(i2c_master_write_read_device(I2C_NUM_0, AL_ACCEL_ADDR, &reg, 1, &val, 1, 1000));
  return val;
}

static void al_accel_check() {
  // read orientation
  uint8_t orientation = al_accel_read(0x28);
  bool front = orientation & 0b1;
  uint16_t rot = al_accel_rot_map[(orientation >> 1) & 0b11];
  bool lock = orientation & 0b1000000;
  if (AL_ACCEL_DEBUG) {
    naos_log("acc: front=%d rot=%d lock=%d", front, rot, lock);
  }

  // update state
  al_accel_state.front = front;
  al_accel_state.rot = rot;
  al_accel_state.lock = lock;
}

static void al_accel_signal() {
  // defer check
  naos_defer_isr(al_accel_check);
}

void al_accel_init() {
  // reset device
  al_accel_write(0x15, 0b10000000);

  // configure interrupt polarity and wake from sleep
  al_accel_write(0x18, 0b00010000);

  // enable orientation interrupt
  al_accel_write(0x20, 0b00001000);

  // enable orientation detection with debounce
  al_accel_write(0x29, 0b01000000);
  al_accel_write(0x2A, 50);

  // activate device
  al_accel_write(0x15, 0b00000001);

  // setup interrupt
  gpio_config_t cfg = {
      .pin_bit_mask = BIT64(AL_ACCEL_INT),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));
  ESP_ERROR_CHECK(gpio_isr_handler_add(AL_ACCEL_INT, al_accel_signal, NULL));

  // clear interrupt
  al_accel_check();
}

al_accel_state_t al_accel_get() {
  // return state
  return al_accel_state;
}
