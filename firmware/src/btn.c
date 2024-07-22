#include <naos.h>
#include <naos/sys.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "sig.h"

#define BTN_A GPIO_NUM_12
#define BTN_B GPIO_NUM_17
#define BTN_C GPIO_NUM_15
#define BTN_D GPIO_NUM_4
#define BTN_E GPIO_NUM_9
#define BTN_F GPIO_NUM_13
#define BTN_REPEAT 750
#define BTN_DEBUG false

static uint8_t btn_state = 0x00;

static sig_type_t btn_map[] = {
    SIG_ENTER, SIG_ESCAPE, SIG_UP, SIG_RIGHT, SIG_DOWN, SIG_LEFT,
};

static int64_t btn_times[8] = {0};
static int8_t btn_counts[8] = {0};

static uint8_t btn_read() {
  // read buttons
  uint8_t a = gpio_get_level(BTN_A) == 0;
  uint8_t b = gpio_get_level(BTN_B) == 0;
  uint8_t c = gpio_get_level(BTN_C) == 0;
  uint8_t d = gpio_get_level(BTN_D) == 0;
  uint8_t e = gpio_get_level(BTN_E) == 0;
  uint8_t f = gpio_get_level(BTN_F) == 0;

  // set state
  return (a << 0) | (b << 1) | (c << 2) | (d << 3) | (e << 4) | (f << 5);
}

static void btn_check() {
  // set state
  uint8_t state = btn_read();

  // get changed buttons
  uint8_t changed = state ^ btn_state;

  // get time
  int64_t now = naos_millis();

  // dispatch button changes
  for (int8_t i = 0; i < 6; i++) {
    if (changed & (1 << i)) {
      // log change
      if (BTN_DEBUG) {
        naos_log("btn: changed %d=%d", i, (btn_state & (1 << i)) != 0);
      }

      // handle key-down
      if ((state & (1 << i)) != 0) {
        // set time
        btn_times[i] = now;
      }

      // handle key-up
      if ((state & (1 << i)) == 0) {
        // dispatch if no repeated events have yet been dispatched
        if (btn_counts[i] == 0) {
          sig_dispatch((sig_event_t){
              .type = btn_map[i],
              .repeat = false,
          });
        }

        // clear time and count
        btn_times[i] = 0;
        btn_counts[i] = 0;
      }
    } else {
      // check repeat
      if (btn_times[i] != 0 && now - btn_times[i] > BTN_REPEAT) {
        // log repeat
        if (BTN_DEBUG) {
          naos_log("btn: repeated %d", i);
        }

        // subtract time
        btn_times[i] += BTN_REPEAT;

        // increment
        btn_counts[i]++;

        // dispatch repeat press
        sig_dispatch((sig_event_t){
            .type = btn_map[i],
            .repeat = true,
        });
      }
    }
  }

  // update state
  btn_state = state;
}

void btn_init() {
  // configure GPIOs
  gpio_config_t cfg = {
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = BIT64(BTN_A) | BIT64(BTN_B) | BIT64(BTN_C) | BIT64(BTN_D) | BIT64(BTN_E) | BIT64(BTN_F),
      .pull_up_en = GPIO_PULLUP_ENABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));

  // configure wakeup source
  ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(cfg.pin_bit_mask, ESP_EXT1_WAKEUP_ANY_LOW));

  // initialize button state
  btn_state = btn_read();

  // start timer
  naos_repeat("btn", 25, btn_check);  // 50 Hz
}
