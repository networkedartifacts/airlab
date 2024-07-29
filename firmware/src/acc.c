#include <naos.h>
#include <naos/sys.h>
#include <driver/i2c.h>

#include "acc.h"

#define ACC_ADDR 0x18
#define ACC_DEBUG false

static void acc_write(uint8_t reg, uint8_t val) {
  // write data
  uint8_t data[2] = {reg, val};
  ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, ACC_ADDR, data, 2, 1000));
}

static void acc_read(uint8_t reg, uint8_t *buf, size_t len) {
  // read data
  ESP_ERROR_CHECK(i2c_master_write_read_device(I2C_NUM_0, ACC_ADDR, &reg, 1, buf, len, 1000));
}

static void acc_check() {
  // read orientation
  uint8_t orientation;
  acc_read(0x28, &orientation, 1);
  if (ACC_DEBUG) {
    acc_face_t face = orientation & 0b1;
    acc_mode_t orient = (orientation >> 1) & 0b11;
    naos_log("acc: face=%d mode=%d", face, orient);
  }
}

void acc_init() {
  // reset device
  acc_write(0x15, 0b10000000);
  naos_delay(20);

  // enable orientation detection
  acc_write(0x30, 1);
  acc_write(0x29, 0b11000000);

  // activate device
  acc_write(0x15, 0b00000001);

  // run check
  naos_repeat("acc", 100, acc_check);
}
