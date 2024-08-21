#include <driver/i2c.h>

#include "led.h"

#define LED_ADDR 0x30

static void led_write(uint8_t reg, uint8_t val, bool may_fail) {
  // write data
  uint8_t data[2] = {reg, val};
  esp_err_t err = i2c_master_write_to_device(I2C_NUM_0, LED_ADDR, data, 2, 1000);
  if (!may_fail || err != ESP_FAIL) {
    ESP_ERROR_CHECK(err);
  }
}

void led_init() {
  // reset chip
  led_write(0x00, 0b00000111, true);

  // disable auto blink
  led_write(0x09, 0b00000110, false);

  // turn LEDs off
  led_set(0, 0, 0);
}

void led_set(float r, float g, float b) {
  // set LEDs on/off
  uint8_t state = (b > 0) | (g > 0) << 2 | (r > 0) << 4;
  led_write(0x04, 0b01000000 | state, false);

  // set color
  led_write(0x01, (uint8_t)(b * 191), false);
  led_write(0x02, (uint8_t)(g * 191), false);
  led_write(0x03, (uint8_t)(r * 191), false);
}
