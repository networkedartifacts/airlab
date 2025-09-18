#include <naos.h>
#include <naos/sys.h>
#include <math.h>
#include <art32/numbers.h>

#include <al/accel.h>
#include <al/buttons.h>
#include <al/touch.h>
#include <al/buzzer.h>
#include <al/led.h>
#include <al/power.h>
#include <al/store.h>

#include "hmi.h"
#include "sig.h"

#define HMI_REPEAT 750
#define HMI_DEBUG false

#define HMI_LED_SLOW 5, 0.1f
#define HMI_LED_FAST 0.3, 0.5f
#define HMI_LED_OFF 0, 0, 0
#define HMI_LED_BLUE 0.8f, 0, 0.7f
#define HMI_LED_RED 0.8f, 0.02f, 0.02f
#define HMI_LED_WHITE .7f, .15f, .2f

static bool hmi_power_light = true;
static naos_mutex_t hmi_mutex;
static float hmi_touch_scroll = 0;
static float hmi_touch_scroll_fast = 0;
static uint8_t hmi_button_state = 0;
static int64_t hmi_button_times[8] = {0};
static int8_t hmi_button_counts[8] = {0};
static uint8_t hmi_flags[HMI_FLAG_MAX] = {0};

static sig_type_t hmi_map[] = {
    SIG_ENTER, SIG_ESCAPE, SIG_UP, SIG_RIGHT, SIG_DOWN, SIG_LEFT,
};

static void hmi_power_hook(al_power_state_t state) {
  // log power state
  if (HMI_DEBUG) {
    naos_log("hmi: power state: usb=%d, charging=%d", state.usb, state.charging);
  }

  // dispatch event
  sig_dispatch((sig_event_t){
      .type = SIG_POWER,
  });
}

static void hmi_accel_hook(al_accel_state_t state) {
  // log accel state
  if (HMI_DEBUG) {
    naos_log("hmi: accel state: front=%d, rotation=%d", state.front, state.rotation);
  }

  // dispatch event
  sig_dispatch((sig_event_t){
      .type = SIG_MOTION,
  });
}

static void hmi_touch_hook(float pos) {
  // prepare state
  static float prev_pos = 0;
  static int64_t prev_time = 0;

  // ignore same position
  if (pos == prev_pos) {
    return;
  }

  // play click
  al_buzzer_click();

  // get time
  int64_t now = naos_millis();

  // calculate delta, if valid
  float delta = 0;
  float delta_fast = 0;
  if (!isnan(prev_pos) && !isnan(pos)) {
    delta = pos - prev_pos;
    delta_fast = delta * a32_safe_map_f((float)(now - prev_time), 0, 500, 4, 1);
  }

  // log
  if (HMI_DEBUG) {
    naos_log("hmi: position=%.1f delta=%.1f delta_fast=%.1f", pos, delta, delta_fast);
  }

  // set state
  prev_pos = pos;
  prev_time = now;

  // update scroll
  naos_lock(hmi_mutex);
  hmi_touch_scroll += delta;
  hmi_touch_scroll_fast += delta_fast;
  naos_unlock(hmi_mutex);

  // dispatch event
  sig_dispatch((sig_event_t){
      .type = SIG_TOUCH,
      .position = pos,
  });
}

static void hmi_touch_check() {
  // capture scroll
  naos_lock(hmi_mutex);
  float scroll = hmi_touch_scroll;
  float scroll_fast = hmi_touch_scroll_fast;
  hmi_touch_scroll = 0;
  hmi_touch_scroll_fast = 0;
  naos_unlock(hmi_mutex);

  // dispatch event, if non-zero
  if (scroll != 0) {
    sig_dispatch((sig_event_t){
        .type = SIG_SCROLL,
        .scroll = scroll,
        .scroll_fast = scroll_fast,
    });
  }
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

static void hmi_led_check() {
  // lock mutex
  naos_lock(hmi_mutex);

  // handle ignore
  if (hmi_flags[HMI_FLAG_IGNORE] > 0) {
    naos_unlock(hmi_mutex);
    return;
  }

  // handle off
  if (hmi_flags[HMI_FLAG_OFF] > 0) {
    al_led_set(HMI_LED_OFF);
    naos_unlock(hmi_mutex);
    return;
  }

  /* handle activity */

  // handle modal
  if (hmi_flags[HMI_FLAG_MODAL] > 0) {
    al_led_set(HMI_LED_BLUE);
    naos_unlock(hmi_mutex);
    return;
  }

  // handle process
  if (hmi_flags[HMI_FLAG_PROCESS] > 0) {
    al_led_flash(HMI_LED_FAST, HMI_LED_BLUE);
    naos_unlock(hmi_mutex);
    return;
  }

  /* handle alerts */

  // get sensor data
  al_sample_t sample = al_store_last();

  // handle high alert
  if (sample.co2 > 1500) {
    al_led_set(HMI_LED_RED);
    naos_unlock(hmi_mutex);
    return;
  }

  // handle elevated alert
  if (sample.co2 > 1000) {
    al_led_flash(HMI_LED_SLOW, HMI_LED_RED);
    naos_unlock(hmi_mutex);
    return;
  }

  /* handle states */

  // get power state
  al_power_state_t state = al_power_get();

  // set LED
  if (hmi_power_light && state.charging) {
    al_led_flash(HMI_LED_SLOW, HMI_LED_WHITE);
  } else if (hmi_power_light && state.usb) {
    al_led_set(HMI_LED_WHITE);
  } else {
    al_led_set(HMI_LED_OFF);
  }

  // unlock mutex
  naos_unlock(hmi_mutex);
}

static naos_param_t hmi_params[] = {
    {.name = "power-light", .type = NAOS_BOOL, .sync_b = &hmi_power_light, .default_b = true}};

void hmi_init() {
  // register parameters
  for (int i = 0; i < sizeof(hmi_params) / sizeof(naos_param_t); i++) {
    naos_register(&hmi_params[i]);
  }

  // create mutex
  hmi_mutex = naos_mutex();

  // register power hook
  al_power_config(hmi_power_hook);

  // register accelerometer hook
  al_accel_config(hmi_accel_hook);

  // get button state
  hmi_button_state = al_buttons_get();

  // register touch hook
  al_touch_config(hmi_touch_hook);

  // create timers
  naos_repeat("hmi-tch", 300, hmi_touch_check);
  naos_repeat("hmi-btn", 25, hmi_button_check);  // 50 Hz
  naos_repeat("hmi-led", 300, hmi_led_check);
}

void hmi_set_flag(hmi_flag_t flag) {
  // increase count
  naos_lock(hmi_mutex);
  hmi_flags[flag]++;
  naos_unlock(hmi_mutex);
}

void hmi_clear_flag(hmi_flag_t flag) {
  // decrease count
  naos_lock(hmi_mutex);
  if (hmi_flags[flag] > 0) {
    hmi_flags[flag]--;
  }
  naos_unlock(hmi_mutex);
}
