#include <esp_err.h>
#include <math.h>
#include <naos.h>

#include <al/core.h>
#include <al/led.h>

// Chip: KTD2037

#define AL_LED_ADDR 0x30
#define AL_LED_LIMIT(val) (val < 0 ? 0 : val > 1 ? 1 : val)

static void al_led_write(uint8_t reg, uint8_t val, bool may_fail) {
  // write data
  uint8_t data[2] = {reg, val};
  esp_err_t err = al_i2c_transfer(AL_LED_ADDR, data, 2, NULL, 0, 1000);
  if (!may_fail || err != ESP_FAIL) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);
  }
}

void al_led_init(bool reset) {
  // perform reset
  if (reset) {
    // reset chip
    al_led_write(0x00, 0b00000111, true);

    // disable auto blink
    al_led_write(0x09, 0b00000110, false);

    // turn LEDs off
    al_led_set(0, 0, 0);
  }
}

void al_led_set(float r, float g, float b) {
  // scale values to 0-17mA
  r *= 0.7f;
  g *= 0.7f;
  b *= 0.7f;

  // set LEDs on/off
  uint8_t state = (b > 0) | (g > 0) << 2 | (r > 0) << 4;
  uint8_t on = (r != 0 || g != 0 || b != 0) ? 0b01000000 : 0;
  al_led_write(0x04, on | state, false);

  // set color
  al_led_write(0x06, (uint8_t)(AL_LED_LIMIT(b) * 191), false);
  al_led_write(0x07, (uint8_t)(AL_LED_LIMIT(g) * 191), false);
  al_led_write(0x08, (uint8_t)(AL_LED_LIMIT(r) * 191), false);
}

void al_led_flash(float p, float f, float r, float g, float b) {
  // configure flash period
  uint8_t period = (uint8_t)((p - 0.256f) / 0.128f);
  al_led_write(0x01, period, false);

  // configure ON percentage
  al_led_write(0x02, (uint8_t)(f * 100.f), false);
  al_led_write(0x03, (uint8_t)(f * 100.f), false);

  // configure rise/fall times
  uint8_t rf = (uint8_t)fminf(p * f / 2.f * 1000.f / 128.f, 15);
  al_led_write(0x05, rf << 4 | rf, false);

  // set LEDs on/off
  uint8_t state = (b > 0) << 1 | (g > 0) << 3 | (r > 0) << 5;
  uint8_t on = (r != 0 || g != 0 || b != 0) ? 0b01000000 : 0;
  al_led_write(0x04, on | state, false);

  // set color
  al_led_write(0x06, (uint8_t)(AL_LED_LIMIT(b) * 191), false);
  al_led_write(0x07, (uint8_t)(AL_LED_LIMIT(g) * 191), false);
  al_led_write(0x08, (uint8_t)(AL_LED_LIMIT(r) * 191), false);
}
