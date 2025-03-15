#include <naos.h>
#include <naos/sys.h>

#include <al/buttons.h>
#include <al/touch.h>
#include <al/buzzer.h>

#include "sig.h"

#define HMI_REPEAT 750
#define HMI_DEBUG false

static naos_mutex_t hmi_mutex;
static float hmi_touch_delta = 0;
static uint8_t hmi_button_state = 0;
static int64_t hmi_button_times[8] = {0};
static int8_t hmi_button_counts[8] = {0};

static sig_type_t hmi_map[] = {
    SIG_ENTER, SIG_ESCAPE, SIG_UP, SIG_RIGHT, SIG_DOWN, SIG_LEFT,
};

static void hmi_touch_hook(al_touch_event_t event) {
  // play click
  al_buzzer_click();

  // stop if not touched
  if (event.touches == 0) {
    return;
  }

  // update delta
  naos_lock(hmi_mutex);
  hmi_touch_delta += event.delta;
  naos_unlock(hmi_mutex);

  // dispatch event
  sig_dispatch((sig_event_t){
      .type = SIG_TOUCH,
      .touch = event.position,
  });
}

static void hmi_touch_check() {
  // capture delta
  naos_lock(hmi_mutex);
  float delta = hmi_touch_delta;
  hmi_touch_delta = 0;
  naos_unlock(hmi_mutex);

  // stop, if zero
  if (delta == 0) {
    return;
  }

  // dispatch event
  sig_dispatch((sig_event_t){
      .type = SIG_SCROLL,
      .touch = delta,
  });
}

static void hmi_button_check() {
  // set state
  uint8_t state = al_buttons_get();

  // get changed buttons
  uint8_t changed = state ^ hmi_button_state;

  // get time
  int64_t now = naos_millis();

  // dispatch button changes
  for (int8_t i = 0; i < 6; i++) {
    if (changed & (1 << i)) {
      // log change
      if (HMI_DEBUG) {
        naos_log("btn: changed %d=%d", i, (hmi_button_state & (1 << i)) != 0);
      }

      // handle key-down
      if ((state & (1 << i)) != 0) {
        // set time
        hmi_button_times[i] = now;
      }

      // handle key-up
      if ((state & (1 << i)) == 0) {
        // dispatch if no repeated events have yet been dispatched
        if (hmi_button_counts[i] == 0) {
          sig_dispatch((sig_event_t){
              .type = hmi_map[i],
              .repeat = false,
          });
        }

        // clear time and count
        hmi_button_times[i] = 0;
        hmi_button_counts[i] = 0;
      }
    } else {
      // check repeat
      if (hmi_button_times[i] != 0 && now - hmi_button_times[i] > HMI_REPEAT) {
        // log repeat
        if (HMI_DEBUG) {
          naos_log("btn: repeated %d", i);
        }

        // subtract time
        hmi_button_times[i] += HMI_REPEAT;

        // increment
        hmi_button_counts[i]++;

        // dispatch repeat press
        sig_dispatch((sig_event_t){
            .type = hmi_map[i],
            .repeat = true,
        });
      }
    }
  }

  // update state
  hmi_button_state = state;
}

void hmi_init() {
  // create mutex
  hmi_mutex = naos_mutex();

  // get button state
  hmi_button_state = al_buttons_get();

  // register touch hook
  al_touch_config(hmi_touch_hook);

  // create timers
  naos_repeat("hmi-tch", 300, hmi_touch_check);
  naos_repeat("hmi-btn", 25, hmi_button_check);  // 50 Hz
}
