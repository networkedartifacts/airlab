#include <naos.h>
#include <naos/sys.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "sig.h"

#define BTN_DEBUG false
#define BTN_WAKE GPIO_NUM_36
#define BTN_REPEAT 750

static uint8_t btn_state = 0x00;

static sig_type_t btn_map[] = {
    SIG_ESCAPE, SIG_LEFT, SIG_RIGHT, SIG_DOWN, SIG_UP, SIG_ENTER,
};

static int64_t btn_times[8] = {0};
static int8_t btn_counts[8] = {0};

static uint8_t dev_shift() {
  return 0;
}

static void btn_check() {
  // read shift register (inverted)
  uint8_t state = dev_shift() ^ 0xFF;

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
  // configure gpio
  gpio_config_t pin = {
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = BIT64(BTN_WAKE),
  };
  ESP_ERROR_CHECK(gpio_config(&pin));

  // configure wakeup source
  ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(BIT64(BTN_WAKE), ESP_EXT1_WAKEUP_ALL_LOW));

  // start timer
  naos_repeat("btn", 25, btn_check);  // 50 Hz
}
