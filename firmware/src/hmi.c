#include <naos.h>
#include <naos/sys.h>

#include <al/accel.h>
#include <al/buttons.h>
#include <al/touch.h>
#include <al/buzzer.h>
#include <al/led.h>
#include <al/power.h>
#include <al/sensor.h>

#include "hmi.h"
#include "rec.h"
#include "sig.h"

#define HMI_REPEAT 750
#define HMI_DEBUG false

#define HMI_LED_SLOW 5, 0.1f
#define HMI_LED_FAST 0.3, 0.5f
#define HMI_LED_OFF 0, 0, 0
#define HMI_LED_BLUE 0.8f, 0, 0.7f
#define HMI_LED_RED 0.8f, 0.02f, 0.02f
#define HMI_LED_WHITE .7f, .15f, .2f

static naos_mutex_t hmi_mutex;
static float hmi_touch_delta = 0;
static uint8_t hmi_button_state = 0;
static int64_t hmi_button_times[8] = {0};
static int8_t hmi_button_counts[8] = {0};
static uint8_t hmi_flags[HMI_FLAG_MAX] = {0};

static sig_type_t hmi_map[] = {
    SIG_ENTER, SIG_ESCAPE, SIG_UP, SIG_RIGHT, SIG_DOWN, SIG_LEFT,
};

static void hmi_accel_hook(al_accel_state_t state) {
  // dispatch event
  sig_dispatch((sig_event_t){
      .type = SIG_MOTION,
  });
}

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

static void hmi_led_check() {
  // lock mutex
  naos_lock(hmi_mutex);

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
  al_sample_t sample = al_sensor_last();

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
  if (state.charging) {
    al_led_flash(HMI_LED_SLOW, HMI_LED_WHITE);
  } else if (state.usb) {
    al_led_set(HMI_LED_WHITE);
  } else {
    al_led_set(HMI_LED_OFF);
  }

  // unlock mutex
  naos_unlock(hmi_mutex);
}

void hmi_init() {
  // create mutex
  hmi_mutex = naos_mutex();

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

void hmi_set_flag(hmi_flag_t state) {
  // increase count
  naos_lock(hmi_mutex);
  hmi_flags[state]++;
  naos_unlock(hmi_mutex);
}

void hmi_clear_flag(hmi_flag_t state) {
  // decrease count
  naos_lock(hmi_mutex);
  if (hmi_flags[state] > 0) {
    hmi_flags[state]--;
  }
  naos_unlock(hmi_mutex);
}
