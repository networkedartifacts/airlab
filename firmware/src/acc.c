#include <naos.h>
#include <naos/sys.h>
#include <driver/i2c.h>

#include "acc.h"
#include "dev.h"

#define ACC_ADDR 0x18

#define ACC_DEBUG false

static acc_state_t acc_state = {0};
static uint16_t acc_rot_map[] = {180, 0, 90, 270};

static void acc_write(uint8_t reg, uint8_t val) {
  // write data
  uint8_t data[2] = {reg, val};
  ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, ACC_ADDR, data, 2, 1000));
}

static uint8_t acc_read(uint8_t reg) {
  uint8_t val;
  ESP_ERROR_CHECK(i2c_master_write_read_device(I2C_NUM_0, ACC_ADDR, &reg, 1, &val, 1, 1000));
  return val;
}

static void acc_check() {
  // read orientation
  uint8_t orientation = acc_read(0x28);
  bool front = orientation & 0b1;
  uint16_t rot = acc_rot_map[(orientation >> 1) & 0b11];
  bool lock = orientation & 0b1000000;
  if (ACC_DEBUG) {
    naos_log("acc: front=%d rot=%d lock=%d", front, rot, lock);
  }

  // update state
  acc_state.front = front;
  acc_state.rot = rot;
  acc_state.lock = lock;
}

static void acc_signal() {
  // defer check
  naos_defer_isr(acc_check);
}

void acc_init() {
  // reset device
  acc_write(0x15, 0b10000000);

  // configure interrupt polarity and wake from sleep
  acc_write(0x18, 0b00010000);

  // enable orientation interrupt
  acc_write(0x20, 0b00001000);

  // enable orientation detection with debounce
  acc_write(0x29, 0b01000000);
  acc_write(0x2A, 50);

  // activate device
  acc_write(0x15, 0b00000001);

  // setup interrupt
  gpio_config_t cfg = {
      .pin_bit_mask = BIT64(DEV_INT_ACC),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));
  ESP_ERROR_CHECK(gpio_isr_handler_add(DEV_INT_ACC, acc_signal, NULL));

  // clear interrupt
  acc_check();
}

acc_state_t acc_get() {
  // return state
  return acc_state;
}
