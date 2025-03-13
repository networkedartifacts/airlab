#include <esp_sleep.h>

#include "dev.h"

void dev_init() {
  // configure wakeup source
  uint64_t pin_mask = BIT64(DEV_BTN_A) | BIT64(DEV_BTN_B) | BIT64(DEV_BTN_C) | BIT64(DEV_BTN_D) | BIT64(DEV_BTN_E) |
                      BIT64(DEV_BTN_F) | BIT64(DEV_INT_ACC);
  ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(pin_mask, ESP_EXT1_WAKEUP_ANY_LOW));
}
