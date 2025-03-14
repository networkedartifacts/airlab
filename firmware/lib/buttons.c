#include <driver/gpio.h>

#include <al/buttons.h>

#include "buttons.h"

void al_buttons_init() {
  // configure GPIOs
  gpio_config_t cfg = {
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = BIT64(AL_BUTTONS_A) | BIT64(AL_BUTTONS_B) | BIT64(AL_BUTTONS_C) | BIT64(AL_BUTTONS_D) |
                      BIT64(AL_BUTTONS_E) | BIT64(AL_BUTTONS_F),
      .pull_up_en = GPIO_PULLUP_ENABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));

  // hold GPIOs during sleep
  ESP_ERROR_CHECK(gpio_hold_en(AL_BUTTONS_A));
  ESP_ERROR_CHECK(gpio_hold_en(AL_BUTTONS_B));
  ESP_ERROR_CHECK(gpio_hold_en(AL_BUTTONS_C));
  ESP_ERROR_CHECK(gpio_hold_en(AL_BUTTONS_D));
  ESP_ERROR_CHECK(gpio_hold_en(AL_BUTTONS_E));
  ESP_ERROR_CHECK(gpio_hold_en(AL_BUTTONS_F));
}

uint8_t al_buttons_get() {
  // read buttons
  uint8_t a = gpio_get_level(AL_BUTTONS_A) == 0;
  uint8_t b = gpio_get_level(AL_BUTTONS_B) == 0;
  uint8_t c = gpio_get_level(AL_BUTTONS_C) == 0;
  uint8_t d = gpio_get_level(AL_BUTTONS_D) == 0;
  uint8_t e = gpio_get_level(AL_BUTTONS_E) == 0;
  uint8_t f = gpio_get_level(AL_BUTTONS_F) == 0;

  // set state
  return (a << 0) | (b << 1) | (c << 2) | (d << 3) | (e << 4) | (f << 5);
}
