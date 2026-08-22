#include <naos.h>
#include <naos/sys.h>
#include <naos/cpu.h>
#include <naos/ble.h>
#include <naos/auth.h>
#include <esp_err.h>
#include <esp_system.h>
#include <lvgl.h>
#include <math.h>
#include <string.h>

#include <al/utils.h>
#include <al/core.h>
#include <al/accel.h>
#include <al/buttons.h>
#include <al/power.h>
#include <al/clock.h>
#include <al/sensor.h>
#include <al/storage.h>
#include <al/store.h>
#include <al/buzzer.h>
#include <al/led.h>

#include "com.h"
#include "gui.h"
#include "gfx.h"
#include "sig.h"
#include "fnt.h"
#include "img.h"
#include "lvx.h"
#include "rec.h"
#include "scr.h"
#include "dev.h"
#include "stm.h"
#include "hmi.h"
#include "dat.h"
#include "pwr.h"
#include "eng.h"
#include "eng_bundle.h"

#define SCR_MSG_TIMEOUT 2000
#define SCR_IDLE_TIMEOUT 30000
#define SCR_ACTION_TIMEOUT 60000
#define SCR_MIN_RESOLUTION 5000
#define SCR_HIST_POINTS 8

#define SCR_MIN(x, y) ((x) < (y) ? (x) : (y))

static stm_action_t scr_action = 0;
DEV_KEEP static uint16_t scr_file = 0;
DEV_KEEP static void* scr_return_timeout = NULL;
DEV_KEEP static void* scr_return_unlock = NULL;
DEV_KEEP static int scr_return_unlock_mask = 0;
DEV_KEEP static int scr_partial_count = 0;
DEV_KEEP static int32_t scr_screen_index = 0;
static int64_t scr_screen_start = 0;
static bool scr_auto_cycle = true;

// The sample field selected by the user. It is shared by the menu and the view
// screen, so that switching between them keeps the same field.
static int8_t scr_field = 0;  // co2, tmp, hum, voc, nox, prs, pm

// The device distinguishes two wake states. It dozes when it woke up on its
// own to perform background work (refresh the display, take a measurement)
// and returns to sleep right after, and it is awake when a user is present or
// a client is connected. Dozing keeps the sleep measurement interval and the
// radios off, while waking up applies the main interval and starts the
// radios. The state is derived from the wake up trigger and is not carried
// across sleep.
typedef enum {
  SCR_DOZE,
  SCR_AWAKE,
} scr_mode_t;

static scr_mode_t scr_mode = SCR_DOZE;

static void scr_wake_up(const char* reason) {
  // skip if already awake
  if (scr_mode == SCR_AWAKE) {
    return;
  }

  // enter awake state
  scr_mode = SCR_AWAKE;
  naos_log("scr: awake reason=%s", reason);

  // apply the main interval, which is the active interval while awake
  al_sensor_set_interval(naos_get_l("main-rate"));

  // measure PM at the main rate while awake
  al_sensor_set_pm_rate(naos_get_l("main-rate"), false);

  // start the radios
  com_start();
}

static const char* scr_field_fmt[] = {
    [AL_SAMPLE_CO2] = "%.0f ppm CO2", [AL_SAMPLE_TMP] = "%.1f °C",  [AL_SAMPLE_HUM] = "%.1f %% RH",
    [AL_SAMPLE_VOC] = "%.0f VOC",     [AL_SAMPLE_NOX] = "%.0f NOx", [AL_SAMPLE_PRS] = "%.0f hPa",
    [AL_SAMPLE_PM] = "%.1f µg/m3 PM",
};

/* Helpers */

static const char* scr_field_str(al_sample_field_t field, float value) {
  // gas indices and PM read as NaN when no reading is available
  if (isnan(value)) {
    if (field == AL_SAMPLE_NOX) {
      return "n/a NOx";
    } else if (field == AL_SAMPLE_PM) {
      return "n/a PM";
    }
    return "n/a VOC";
  }

  return lvx_fmt(scr_field_fmt[field], value);
}

static void scr_field_cycle(bool forward, bool has_pm) {
  // determine last field (PM is only offered if available)
  int8_t last = has_pm ? AL_SAMPLE_PM : AL_SAMPLE_PRS;

  // cycle field
  if (forward) {
    scr_field++;
    if (scr_field > last) {
      scr_field = 0;
    }
  } else {
    scr_field--;
    if (scr_field < 0) {
      scr_field = last;
    }
  }
}

static void scr_field_check(bool has_pm) {
  // leave the PM field if it became unavailable
  if (scr_field == AL_SAMPLE_PM && !has_pm) {
    scr_field = 0;
  }
}

static const char* scr_ms2str(int32_t ms) {
  if (ms > 1000 * 60 * 60) {  // hours
    return lvx_fmt("%dh", ms / 1000 / 60 / 60);
  } else if (ms > 1000 * 60) {  // minutes
    return lvx_fmt("%dm", ms / 1000 / 60);
  } else {  // seconds
    return lvx_fmt("%ds", ms / 1000);
  }
}

static float scr_temp_convert(float celsius) {
  // convert to fahrenheit if setting is enabled
  if (naos_get_b("fahrenheit")) {
    return celsius * 9.0f / 5.0f + 32.0f;
  }

  return celsius;
}

static const char* scr_temp_format() {
  // return appropriate format string
  if (naos_get_b("fahrenheit")) {
    return "%.1f °F";
  }

  return "%.1f °C";
}

static void scr_power_off(bool low_power, bool msg) {
  // set off flag
  hmi_set_flag(HMI_FLAG_OFF);

  // cleanup screen
  gui_cleanup(true);

  // write message
  if (msg) {
    gui_write(low_power ? "Low Battery\n\nCharge via USB-C and press <A>." : "Powered Off\n\nPress <A> to start.",
              true);
  }

  // clear returns
  scr_return_timeout = NULL;
  scr_return_unlock = NULL;
  scr_return_unlock_mask = 0;

  // power off
  al_power_off();
}

static void scr_launch(const char* file, const char* mode) {
  // prepare flag
  bool loading = true;

  for (;;) {
    // write message on first load or re-launch
    if (loading) {
      gui_cleanup(false);
      gui_write("Loading plugin...", false);
    }

    // determine if screen
    bool screen = strcmp(mode, "screen") == 0;

    // run plugin
    bool ok = eng_run(file, mode);

    // await button, sensor, interrupt, or re-launch for screens
    if (screen && ok) {
      // await event
      sig_event_t event = sig_await(SIG_KEYS | SIG_SENSOR | SIG_INTERRUPT | SIG_LAUNCH, 0);

      // handle launch
      if (event.type == SIG_LAUNCH) {
        file = event.plugin.file;
        mode = event.plugin.mode;
        loading = true;
        continue;
      }

      // handle sensor data and interrupts
      if (event.type & (SIG_SENSOR | SIG_INTERRUPT)) {
        loading = false;
        continue;
      }
    }

    // clean screen
    gui_cleanup(false);

    // show message on failure
    if (!ok) {
      gui_message("Failed to run plugin!", SCR_MSG_TIMEOUT);
    }

    break;
  }
}

// Reports why the device stays awake in its current state, or NULL if it goes
// to sleep instead. The idle screen consults this before drawing, to hide the
// connectivity icons that do not survive the sleep.
static const char* scr_stay_awake() {
  // read power state
  al_power_state_t power = al_power_get();

  // check BLE and MQTT
  bool has_ble = naos_get_b("ble-prev-sleep") && naos_ble_connections() > 0;
  bool has_mqtt = naos_get_b("mqtt-prev-sleep") && naos_status() == NAOS_NETWORKED;

  // check if sleep is prevented at power-test level 2
  if (naos_get_l("power-test") >= 2) {
    return "test";
  }

  // check if connected via BLE/MQTT
  if (has_ble) {
    return "ble";
  } else if (has_mqtt) {
    return "mqtt";
  }

  // check if powered (in hi-Z mode the device runs from the battery, so an
  // attached USB cable should not prevent sleep)
  if (power.has_usb && !power.hiz) {
    return "usb";
  }

  return NULL;
}

static sig_event_t scr_idle_sleep() {
  // read power state
  al_power_state_t power = al_power_get();

  // power off (no return) if battery is low and not charging
  if (power.bat_low && !power.has_usb && !power.charging) {
    scr_power_off(true, true);
  }

  // set sensor gas window and grace also while staying awake, the sensor
  // monitor re-applies the configuration if it changed
  al_sensor_set_gas_window(naos_get_l("gas-window"));
  al_sensor_set_gas_grace(naos_get_l("gas-grace"));

  // check if the device stays awake
  const char* stay_awake = scr_stay_awake();
  if (stay_awake != NULL) {
    // the device stays awake, so wake up fully
    scr_wake_up(stay_awake);

    // wait some time
    sig_event_t event = sig_await(SIG_KEYS | SIG_TIMEOUT | SIG_INTERRUPT | SIG_LAUNCH | SIG_REFRESH, 60 * 1000);

    // start engine on launch
    if (event.type == SIG_LAUNCH) {
      scr_launch(event.plugin.file, event.plugin.mode);
    }

    return event;
  }

  // enter doze state, so an abort below or a later awake condition performs a
  // real transition and re-applies the awake configuration
  scr_mode = SCR_DOZE;

  // set sensor interval
  al_sensor_set_interval(naos_get_l(rec_running() ? "record-rate" : "sleep-rate"));

  // set the PM rate in manual mode, so the sensor never runs on its own while
  // dozing and measurements are taken below before sleeping (this must stay
  // below the awake return above, as scr_wake_up() applies the awake rate only
  // on the state transition and would not restore it)
  al_sensor_set_pm_rate(naos_get_l("pm-rate"), true);

  // finish a PM measurement before sleeping if one is due, the sensor itself
  // is idled by the sleep
  if (al_sensor_pm_due() == 0) {
    al_sensor_pm_measure();
  }

  // check for a key press that arrived while preparing, as a PM measurement
  // may have extended the wake by several seconds, and wake up instead of
  // sleeping through it (other events are dropped, the sleep discards them
  // anyway)
  sig_event_t pending = sig_await(SIG_KEYS, 10);
  if (pending.type & SIG_KEYS) {
    scr_wake_up("key");
    return pending;
  }

  // determine display interval (a full ULP reading buffer may wake us earlier)
  int32_t display_interval = naos_get_l("display-rate");
  if (display_interval < SCR_DISPLAY_MIN) {
    display_interval = SCR_DISPLAY_MIN;
  } else if (display_interval > SCR_DISPLAY_MAX) {
    display_interval = SCR_DISPLAY_MAX;
  }

  // wake earlier if a PM measurement becomes due, as readings are only cached
  // for twice the rate and would otherwise not cover the whole sleep, bounded
  // to not wake up too often
  int32_t pm_due = al_sensor_pm_due();
  if (pm_due < display_interval) {
    display_interval = pm_due < 60 ? 60 : pm_due;
  }

  // sleep until next display refresh (no return)
  al_sleep(true, display_interval * 1000);

  return (sig_event_t){
      .type = SIG_TIMEOUT,
  };
}

/* Translations */

#include "scr_trans.inc"

scr_lang_t scr_lang() {
  // get language
  const char* lang = naos_get_s("language");
  if (strcmp(lang, "en") == 0) {
    return SCR_EN;
  } else if (strcmp(lang, "de") == 0) {
    return SCR_DE;
  } else if (strcmp(lang, "es") == 0) {
    return SCR_ES;
  } else if (strcmp(lang, "fr") == 0) {
    return SCR_FR;
  }
  return SCR_EN;
}

static const scr_trans_t* scr_trans() {
  // return translation
  return &scr_trans_map[scr_lang()];
}

/* Formatters */

static const char* scr_file_name(dat_file_t* file) {
  // return name
  return lvx_fmt(scr_trans()->measurement, file->head.num);
}

static const char* scr_file_date(dat_file_t* file) {
  // get date
  uint16_t year, month, day;
  al_clock_epoch_date(file->head.start, &year, &month, &day);

  // format date
  return lvx_fmt("%d-%02d-%02d", year, month, day);
}

static const char* scr_file_info(dat_file_t* file) {
  // return info
  return lvx_fmt("%s / %s", scr_file_date(file), scr_ms2str(file->stop));
}

/* Screens */

static void* scr_view();
static void* scr_edit();
static void* scr_explore();
static void* scr_menu();
static void* scr_settings();
static void* scr_config();
static void* scr_develop();

static bool scr_time() {
  // begin draw
  gfx_begin(false, false);

  // add row
  lv_obj_t* row = lv_obj_create(lv_scr_act());
  lv_obj_set_size(row, 200, 100);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_pad_row(row, 5, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);

  // prepare wheels
  lvx_wheel_t hour = {.value = 12, .min = 0, .max = 23, .format = "%02d", .fixed = true};
  lvx_wheel_t minute = {.value = 30, .min = 0, .max = 59, .format = "%02d", .fixed = true};

  // assign current time
  uint16_t cur_hour, cur_minute, seconds;
  al_clock_get_time(&cur_hour, &cur_minute, &seconds);
  hour.value = cur_hour;
  minute.value = cur_minute;

  // add wheels
  lvx_wheel_create(&hour, row);
  lvx_wheel_create(&minute, row);

  // add button
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->cancel,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_t next = {
      .title = "A",
      .text = scr_trans()->next,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&next, lv_scr_act());

  // focus first wheel
  lvx_wheel_focus(&hour, true);

  // end draw
  gfx_end(false, false);

  // prepare list
  lvx_wheel_t* wheels[] = {&hour, &minute};
  int cur_wheel = 0;

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_KEYS | SIG_SCROLL, SCR_ACTION_TIMEOUT);

    // apply wheel events
    if (event.type & (SIG_ARROWS | SIG_SCROLL)) {
      gfx_begin(false, false);
      lvx_wheel_group_update(wheels, 2, event, &cur_wheel);
      gfx_end(false, false);
      continue;
    }

    // cleanup
    gui_cleanup(false);

    // handle escape/timeout event
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return false;
    }

    /* handle enter */

    // save time
    al_clock_set_time(hour.value, minute.value, 0);

    return true;
  }
}

static bool scr_date() {
  // begin draw
  gfx_begin(false, false);

  // add row
  lv_obj_t* row = lv_obj_create(lv_scr_act());
  lv_obj_set_size(row, 200, 100);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_pad_row(row, 5, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);

  // prepare wheels
  lvx_wheel_t year = {.value = 2023, .min = 2023, .max = 2999, .fixed = true};
  lvx_wheel_t month = {.value = 6, .min = 1, .max = 12, .format = "%02d", .fixed = true};
  lvx_wheel_t day = {.value = 15, .min = 1, .max = 31, .format = "%02d", .fixed = true};

  // assign current date
  uint16_t cur_year, cur_month, cur_day;
  al_clock_get_date(&cur_year, &cur_month, &cur_day);
  year.value = cur_year;
  month.value = cur_month;
  day.value = cur_day;

  // add wheels
  lvx_wheel_create(&year, row);
  lvx_wheel_create(&month, row);
  lvx_wheel_create(&day, row);

  // add button
  lvx_sign_t next = {
      .title = "A",
      .text = scr_trans()->next,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t off = {
      .title = "B",
      .text = scr_trans()->cancel,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&next, lv_scr_act());
  lvx_sign_create(&off, lv_scr_act());

  // focus first wheel
  lvx_wheel_focus(&year, true);

  // end draw
  gfx_end(false, false);

  // prepare wheels
  lvx_wheel_t* wheels[] = {&year, &month, &day};
  int cur_wheel = 0;

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_KEYS | SIG_SCROLL, SCR_ACTION_TIMEOUT);

    // apply wheel events
    if (event.type & (SIG_ARROWS | SIG_SCROLL)) {
      gfx_begin(false, false);
      lvx_wheel_group_update(wheels, 3, event, &cur_wheel);
      gfx_end(false, false);
      continue;
    }

    // cleanup
    gui_cleanup(false);

    // return on escape/timeout
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return false;
    }

    /* handle enter */

    // save date
    al_clock_set_date(year.value, month.value, day.value);

    return true;
  }
}

static void* scr_info() {
  // begin draw
  gfx_begin(false, false);

  // add label
  lv_obj_t* label = lv_label_create(lv_scr_act());
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(label, 6, LV_PART_MAIN);

  // end draw
  gfx_end(true, false);

  for (;;) {
    // get power
    al_power_state_t bat = al_power_get();

    // get date and time
    uint16_t year, month, day, hour, minute, seconds;
    al_clock_get_date(&year, &month, &day);
    al_clock_get_time(&hour, &minute, &seconds);

    // get CPU usage
    float cpu0 = 0, cpu1 = 0;
    naos_cpu_get(&cpu0, &cpu1);

    // prepare text
    const char* text = lvx_fmt("%llds - %.0f%% - P%d - F%d\n%04d-%02d-%02d %02d:%02d:%02d\n%lu kB - %.1f%% - %.1f%%",
                               naos_millis() / 1000, bat.bat_level * 100, bat.has_usb, bat.can_fast, year, month, day,
                               hour, minute, seconds, esp_get_free_heap_size() / 1024, cpu0 * 100, cpu1 * 100);

    // update label
    gfx_begin(false, false);
    lv_label_set_text(label, text);
    gfx_end(false, false);

    // await event
    sig_event_t event = sig_await(SIG_KEYS, 1000);

    // loop on timeout
    if (event.type == SIG_TIMEOUT) {
      continue;
    }

    // cleanup
    gui_cleanup(false);

    return scr_develop;
  }
}

static void* scr_sensor() {
  // begin draw
  gfx_begin(false, false);

  // add label
  lv_obj_t* label = lv_label_create(lv_scr_act());
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(label, 6, LV_PART_MAIN);

  // end draw
  gfx_end(true, false);

  for (;;) {
    // get last sample and counts
    al_sample_t last = al_store_last();
    int num_short = (int)al_store_count(AL_STORE_SHORT);
    int num_long = (int)al_store_count(AL_STORE_LONG);

    // sample values
    float co2 = al_sample_read(last, AL_SAMPLE_CO2);
    float tmp = al_sample_read(last, AL_SAMPLE_TMP);
    float hum = al_sample_read(last, AL_SAMPLE_HUM);
    float voc = al_sample_read(last, AL_SAMPLE_VOC);
    float nox = al_sample_read(last, AL_SAMPLE_NOX);
    float prs = al_sample_read(last, AL_SAMPLE_PRS);

    // prepare text
    const char* voc_str = isnan(voc) ? "n/a" : lvx_fmt("%.0f", voc);
    const char* nox_str = isnan(nox) ? "n/a" : lvx_fmt("%.0f", nox);
    const char* text =
        lvx_fmt("%.0f ppm, %.2f °C, %.0f %%RH\n%s VOC, %s NOX, %.0f hPa\n\nShort: %d/%d, Long: %d/%d", co2, tmp, hum,
                voc_str, nox_str, prs, num_short, AL_STORE_NUM_SHORT, num_long, AL_STORE_NUM_LONG);

    // update label
    gfx_begin(false, false);
    lv_label_set_text(label, text);
    gfx_end(false, false);

    // await event
    sig_event_t event = sig_await(SIG_KEYS, 1000);

    // loop on timeout
    if (event.type == SIG_TIMEOUT) {
      continue;
    }

    // cleanup
    gui_cleanup(false);

    return scr_develop;
  }
}

static void* scr_idle() {
  // local state
  static bool wakeup_handled = false;

  // handle button wake from deep sleep (once)
  if (!wakeup_handled && naos_millis() < 3000) {
    wakeup_handled = true;
    uint8_t wakeup = al_buttons_wakeup();
    if (wakeup & (1 << AL_BUTTON_LEFT)) {
      scr_screen_index--;
    } else if (wakeup & (1 << AL_BUTTON_RIGHT)) {
      scr_screen_index++;
    }
  }

  // set return handler
  scr_return_timeout = scr_idle;

  // only unlock on a subset of buttons
  scr_return_unlock_mask =
      (1 << AL_BUTTON_ENTER) | (1 << AL_BUTTON_ESCAPE) | (1 << AL_BUTTON_UP) | (1 << AL_BUTTON_DOWN);

  /* Multi-Screen Idle */

  // track screen start time
  scr_screen_start = naos_millis();

  // track skipped screens
  uint16_t skipped = 0;

  for (;;) {
    // load screen bundle
    eng_bundle_t* screens = eng_bundle_load("config", "screens.alb");
    if (!screens) {
      break;
    }

    // count screens
    uint16_t count = 0;
    for (int i = 0; i < screens->sections_num; i++) {
      if (screens->sections[i].type == ENG_BUNDLE_TYPE_ATTR) {
        count++;
      }
    }

    // check if there are screens
    if (count == 0) {
      eng_bundle_free(screens);
      break;
    }

    // wrap index
    if (scr_screen_index < 0) {
      scr_screen_index = count - 1;
    } else if (scr_screen_index >= count) {
      scr_screen_index = 0;
    }

    // update parameter
    naos_set_l("idle-scr-index", scr_screen_index);

    // find nth ATTR section
    const char* file = NULL;
    const char* key = NULL;
    uint16_t n = 0;
    for (int i = 0; i < screens->sections_num; i++) {
      if (screens->sections[i].type == ENG_BUNDLE_TYPE_ATTR) {
        if (n == scr_screen_index) {
          key = screens->sections[i].name;
          file = eng_bundle_read(screens, &screens->sections[i]);
          break;
        }
        n++;
      }
    }

    // parse args from config section
    size_t a_len = 0;
    void* a_data = NULL;
    if (key) {
      a_data = eng_bundle_config(screens, key, &a_len);
    }

    // parse args bundle if present
    eng_bundle_t* args = NULL;
    if (a_data && a_len > 0) {
      args = eng_bundle_parse(a_data, a_len);
    }

    // advance index if 60s have elapsed and cycling is enabled
    if (scr_auto_cycle && naos_millis() - scr_screen_start >= 60 * 1000) {
      scr_screen_index++;
      scr_screen_start = naos_millis();
    }

    // run screen
    bool ok = eng_run_config(file, "screen", args);

    // free args bundle
    if (args) {
      eng_bundle_free(args);
    }

    // free screens bundle
    eng_bundle_free(screens);

    // skip to next screen if it failed to run
    if (!ok) {
      gui_cleanup(false);
      skipped++;
      if (skipped >= count) {
        break;
      }
      scr_screen_index++;
      scr_screen_start = naos_millis();
      continue;
    }

    // reset skipped counter
    skipped = 0;

    // sleep until woken
    sig_event_t event = scr_idle_sleep();

    // handle left/right
    if (event.type == SIG_LEFT) {
      scr_screen_index--;
      scr_screen_start = naos_millis();
      continue;
    } else if (event.type == SIG_RIGHT) {
      scr_screen_index++;
      scr_screen_start = naos_millis();
      continue;
    }

    // exit on other keys
    if (event.type & SIG_KEYS) {
      scr_wake_up("key");
      gui_cleanup(false);
      if (scr_return_unlock == NULL) {
        return scr_menu;
      }
      return scr_return_unlock;
    }
  }

  /* Default Screen */

  // begin draw
  gfx_begin(false, false);

  // cleanup any previous plugin screen
  lvx_cleanup();

  // add status
  lvx_status_t status = {0};
  lvx_status_create(&status, lv_scr_act());

  // add values
  lv_obj_t* time = lv_label_create(lv_scr_act());
  lv_obj_t* co2 = lv_label_create(lv_scr_act());
  lv_obj_t* tmp = lv_label_create(lv_scr_act());
  lv_obj_t* hum = lv_label_create(lv_scr_act());
  lv_obj_t* gas = lv_label_create(lv_scr_act());
  lv_obj_t* pm = lv_label_create(lv_scr_act());
  lv_obj_t* prs = lv_label_create(lv_scr_act());

  // add big values
  lv_obj_t* co2_big = lv_label_create(lv_scr_act());
  lv_obj_t* tmp_big = lv_label_create(lv_scr_act());
  lv_obj_t* hum_big = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(co2_big, &fnt_24, LV_PART_MAIN);
  lv_obj_set_style_text_font(tmp_big, &fnt_24, LV_PART_MAIN);
  lv_obj_set_style_text_font(hum_big, &fnt_24, LV_PART_MAIN);

  // end draw
  gfx_end(true, false);

  for (;;) {
    // get time
    uint16_t hour, minute, seconds;
    al_clock_get_time(&hour, &minute, &seconds);

    // get last sample
    al_sample_t sample = al_store_last();

    // await sample, if invalid (after reset)
    if (!al_sample_valid(sample)) {
      sig_await(SIG_SENSOR, 0);
      sample = al_store_last();
    }

    // get accelerometer state
    al_accel_state_t acc = al_accel_get();

    // force full refresh periodically to prevent burn-in
    scr_partial_count++;
    bool refresh = scr_partial_count >= 60;
    if (refresh) {
      scr_partial_count = 0;
    }

    // begin draw
    gfx_begin(refresh, false);

    // determine vertical
    bool vertical = acc.rotation == 90 || acc.rotation == 270;

    // set display rotation
    lv_disp_set_rotation(NULL, acc.rotation / 90);

    // update status, hiding the connectivity icons if the device goes to
    // sleep after this draw, as the radios do not survive the sleep
    status.offline = scr_stay_awake() == NULL;
    lvx_status_update(&status);

    // update values
    lv_label_set_text(time, lvx_fmt("%02d:%02d", hour, minute));
    if (vertical) {
      lv_label_set_text(co2, "ppm CO2");
      lv_label_set_text(tmp, naos_get_b("fahrenheit") ? "° Fahrenheit" : "° Celsius");
      lv_label_set_text(hum, "% RH");
      lv_label_set_text(co2_big, lvx_fmt("%.0f", al_sample_read(sample, AL_SAMPLE_CO2)));
      lv_label_set_text(tmp_big, lvx_fmt("%.1f", scr_temp_convert(al_sample_read(sample, AL_SAMPLE_TMP))));
      lv_label_set_text(hum_big, lvx_fmt("%.1f", al_sample_read(sample, AL_SAMPLE_HUM)));
    } else {
      lv_label_set_text(co2, lvx_fmt("%.0f ppm", al_sample_read(sample, AL_SAMPLE_CO2)));
      lv_label_set_text(tmp, lvx_fmt(scr_temp_format(), scr_temp_convert(al_sample_read(sample, AL_SAMPLE_TMP))));
      lv_label_set_text(hum, lvx_fmt("%.1f%% RH", al_sample_read(sample, AL_SAMPLE_HUM)));
    }
    float voc_val = al_sample_read(sample, AL_SAMPLE_VOC);
    float nox_val = al_sample_read(sample, AL_SAMPLE_NOX);
    float pm_val = al_sample_read(sample, AL_SAMPLE_PM);
    bool has_pm = al_sensor_pm_present() || !isnan(pm_val);
    lv_label_set_text(gas, isnan(voc_val) ? "n/a VOC" : lvx_fmt("%.0f VOC", voc_val));
    lv_label_set_text(pm, isnan(nox_val) ? "n/a NOX" : lvx_fmt("%.0f NOX", nox_val));
    if (has_pm) {
      // with PM, show it in place of the pressure
      lv_label_set_text(prs, isnan(pm_val) ? "n/a PM" : lvx_fmt("%.1f µg/m3 PM", pm_val));
    } else {
      lv_label_set_text(prs, lvx_fmt("%.0f hPa", al_sample_read(sample, AL_SAMPLE_PRS)));
    }

    // align objects
    if (vertical) {
      lv_obj_align(co2_big, LV_ALIGN_TOP_MID, 0, 20);
      lv_obj_align(co2, LV_ALIGN_TOP_MID, 0, 20 + 27);
      lv_obj_align(tmp_big, LV_ALIGN_TOP_MID, 0, 79);
      lv_obj_align(tmp, LV_ALIGN_TOP_MID, 0, 79 + 27);
      lv_obj_align(hum_big, LV_ALIGN_TOP_MID, 0, 136);
      lv_obj_align(hum, LV_ALIGN_TOP_MID, 0, 136 + 27);
      lv_obj_align(gas, LV_ALIGN_BOTTOM_MID, 0, -85);
      lv_obj_align(pm, LV_ALIGN_BOTTOM_MID, 0, -65);
      lv_obj_align(prs, LV_ALIGN_BOTTOM_MID, 0, -45);
      lv_obj_align(time, LV_ALIGN_BOTTOM_RIGHT, -25, -13);
      lv_obj_align(status.row, LV_ALIGN_BOTTOM_LEFT, 25, -15);
    } else {
      lv_obj_align(status.row, LV_ALIGN_TOP_LEFT, 20, 19);
      lv_obj_align(co2, LV_ALIGN_TOP_LEFT, 19, 53);
      lv_obj_align(tmp, LV_ALIGN_TOP_LEFT, 19, 74);
      lv_obj_align(hum, LV_ALIGN_TOP_LEFT, 19, 95);
      lv_obj_align(time, LV_ALIGN_TOP_LEFT, 148, 21);
      lv_obj_align(gas, LV_ALIGN_TOP_LEFT, 148, 53);
      lv_obj_align(pm, LV_ALIGN_TOP_LEFT, 148, 74);
      lv_obj_align(prs, LV_ALIGN_TOP_LEFT, 148, 95);
      lv_obj_align(co2_big, 0, -100, -100);
      lv_obj_align(tmp_big, 0, -100, -100);
      lv_obj_align(hum_big, 0, -100, -100);
    }

    // end draw
    gfx_end(false, true);

    // sleep until next update
    sig_event_t event = scr_idle_sleep();

    // restart on launch (plugin cleaned up screen) or refresh
    if (event.type == SIG_LAUNCH || event.type == SIG_REFRESH) {
      return scr_idle;
    }

    // exit on keys
    if (event.type & SIG_KEYS) {
      scr_wake_up("key");
      break;
    }
  }

  // cleanup
  gui_cleanup(false);

  // default to menu if return was cleared
  if (scr_return_unlock == NULL) {
    return scr_menu;
  }

  return scr_return_unlock;
}

static void* scr_view() {
  // prepare variables
  static bool precision = false;

  // allocate sample buffer
  static al_sample_t* samples = NULL;
  if (samples == NULL) {
    samples = al_calloc(LVX_CHART_SIZE, sizeof(al_sample_t));
  }

  // prepare chart buffer
  static lv_color_t* chart_buffer = NULL;
  if (chart_buffer == NULL) {
    chart_buffer = al_calloc(1, LV_CANVAS_BUF_SIZE_TRUE_COLOR(288, 96));
  }

  // clear memory
  memset(samples, 0, LVX_CHART_SIZE * sizeof(al_sample_t));
  memset(chart_buffer, 0, LV_CANVAS_BUF_SIZE_TRUE_COLOR(288, 96));

  // find file, if not live
  dat_file_t* file = NULL;
  if (scr_file != 0) {
    file = dat_find(scr_file, NULL);
    if (file == NULL) {
      ESP_ERROR_CHECK(ESP_FAIL);
    }
  }

  // check recording
  bool recording = rec_running() && rec_file() == scr_file;

  // begin draw
  gfx_begin(false, false);

  // add bar
  lvx_bar_t bar = {0};
  lvx_bar_create(&bar, lv_scr_act());

  // add chart
  lv_obj_t* canvas = lv_canvas_create(lv_scr_act());
  lv_canvas_set_buffer(canvas, chart_buffer, 288, 96, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(canvas, LV_ALIGN_BOTTOM_LEFT, 5, -5);
  lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

  // end draw
  gfx_end(true, false);

  // prepare deadline
  int64_t deadline = naos_millis() + SCR_IDLE_TIMEOUT;

  // prepare source
  al_sample_source_t source = {0};
  if (file == NULL) {
    source = al_store_source();
  } else {
    source = dat_source(scr_file);
  }

  // prepare position
  int32_t position = 0;
  if (!recording) {
    position = source.stop(source.ctx);
  }

  // cache tracking for sample queries
  int32_t last_start = -1;
  int32_t last_resolution = -1;
  size_t last_source_count = 0;
  size_t num = 0;

  for (;;) {
    // get source info
    size_t source_count = source.count(source.ctx);
    int64_t source_start = source.start(source.ctx);
    int32_t source_stop = source.stop(source.ctx);

    // update recording
    recording = rec_running() && rec_file() == scr_file;

    // adjust position if recording
    if (recording) {
      position = source_stop;
    }

    // calculate resolution
    int32_t resolution = source_stop / LVX_CHART_SIZE;
    if (recording) {
      resolution = SCR_MIN_RESOLUTION;
    } else if (precision) {
      resolution = source_stop / 10 / LVX_CHART_SIZE;
    }
    if (resolution < SCR_MIN_RESOLUTION) {
      resolution = SCR_MIN_RESOLUTION;
    }

    // calculate range
    int32_t start = 0;
    int32_t end = LVX_CHART_SIZE * resolution;
    if (recording) {
      start = position - LVX_CHART_SIZE / 3 * 2 * resolution;
      end = position + LVX_CHART_SIZE / 3 * resolution;
      if (start < 0) {
        end += start * -1;
        start = 0;
      }
    } else if (precision) {
      start = position - LVX_CHART_SIZE / 2 * resolution;
      end = position + LVX_CHART_SIZE / 2 * resolution;
      if (start < 0) {
        end += start * -1;
        start = 0;
      }
      if (end > source_stop) {
        int32_t shift = SCR_MIN(start, end - source_stop);
        end -= shift;
        start -= shift;
      }
    }

    // calculate index
    size_t index = (size_t)roundf(al_safe_map((float)position, (float)start, (float)end, 0, LVX_CHART_SIZE - 1));

    // query samples (only if parameters changed)
    lvx_modal_t modal = {0};
    if (source_count > 0 &&
        (start != last_start || resolution != last_resolution || source_count != last_source_count)) {
      if (al_sample_count(&source, start, start + LVX_CHART_SIZE * resolution) > 4096) {
        gfx_begin(false, false);
        lvx_modal_show(&modal, "Loading samples...");
        gfx_end(false, false);
      }
      num = al_sample_query(&source, samples, LVX_CHART_SIZE, start, resolution);
      if (modal._bg != NULL) {
        gfx_begin(false, false);
        lvx_modal_clear(&modal);
        gfx_end(false, false);
      }
      last_start = start;
      last_resolution = resolution;
      last_source_count = source_count;
    }
    if (recording) {
      index = num > 0 ? num - 1 : 0;
    }

    // ensure index is within valid sample range
    if (index >= num) {
      index = num > 0 ? num - 1 : 0;
    }

    // find marks
    uint8_t marks[LVX_CHART_SIZE] = {0};
    if (file != NULL) {
      for (uint8_t i = 0; i < DAT_MARKS; i++) {
        if (file->head.marks[i] > 0) {
          int32_t mark =
              (int32_t)roundf(al_map((float)file->head.marks[i], (float)start, (float)end, 0, LVX_CHART_SIZE - 1));
          if (mark >= 0 && mark <= LVX_CHART_SIZE - 1) {
            marks[(size_t)mark] = i + 1;
          }
        }
      }
    }

    // determine PM availability; the live view offers the field already with a
    // present chip before data has arrived, matching the menu
    bool has_pm = file == NULL && al_sensor_pm_present();
    for (size_t i = 0; i < num && !has_pm; i++) {
      if (samples[i].pm >= 0) {
        has_pm = true;
      }
    }
    scr_field_check(has_pm);

    // select current sample
    al_sample_t current = samples[index];

    // parse time
    uint16_t hour;
    uint16_t minute;
    al_clock_epoch_time(source_start + (int64_t)current.off, &hour, &minute, NULL);

    // begin draw
    gfx_begin(false, precision);

    // update bar
    bar.time = lvx_fmt("%02d:%02d", hour, minute);
    if (file != NULL) {
      if (recording) {
        bar.mark = file->marks > 0 ? lvx_fmt("M%d", file->marks) : "";
      } else {
        bar.mark = marks[index] > 0 ? lvx_fmt("M%d", marks[index]) : "";
      }
    }
    if (scr_field == AL_SAMPLE_TMP) {
      bar.value = lvx_fmt(scr_temp_format(), scr_temp_convert(al_sample_read(current, scr_field)));
    } else {
      bar.value = scr_field_str(scr_field, al_sample_read(current, scr_field));
    }
    lvx_bar_update(&bar);

    // check fahrenheit
    bool fahrenheit = naos_get_b("fahrenheit");

    // prepare range
    float range = 100;  // hum
    if (scr_field == AL_SAMPLE_CO2) {
      range = 3000;
    } else if (scr_field == AL_SAMPLE_TMP) {
      range = fahrenheit ? 120 : 50;
    } else if (scr_field == AL_SAMPLE_VOC) {
      range = 500;
    } else if (scr_field == AL_SAMPLE_NOX) {
      range = 50;
    } else if (scr_field == AL_SAMPLE_PRS) {
      range = 1500;
    } else if (scr_field == AL_SAMPLE_PM) {
      range = 50;
    }

    // collect values and flags
    float values[LVX_CHART_SIZE] = {0};
    uint8_t flags[LVX_CHART_SIZE] = {0};
    for (size_t i = 0; i < num; i++) {
      al_sample_t sample = samples[i];
      values[i] = al_sample_read(sample, scr_field);
      flags[i] = al_sample_flags(sample, scr_field) != 0;
      if (scr_field == AL_SAMPLE_TMP && fahrenheit) {
        values[i] = scr_temp_convert(values[i]);
      }
      if (values[i] > range) {
        range = values[i];
      }
    }

    // draw chart
    lvx_chart_draw((lvx_chart_t){
        .canvas = canvas,
        .range = range,
        .values = values,
        .flags = flags,
        .marks = marks,
        .arrows = precision,
        .offset = source_start,
        .start = start,
        .end = end,
        .stop = source_stop,
        .cursor = !recording,
        .index = (int)index,
    });

    // end draw
    gfx_end(false, false);

    // await event
    sig_type_t filter = SIG_KEYS | SIG_SCROLL;
    if (file == NULL) {
      filter |= SIG_SENSOR;
    } else if (recording) {
      filter |= SIG_APPEND | SIG_STOP;
    }
    sig_event_t event = sig_await(filter, 10 * 1000);

    // handle deadline
    if (event.type & (SIG_SENSOR | SIG_APPEND) && naos_millis() > deadline) {
      event.type = SIG_TIMEOUT;
    } else if (event.type & (SIG_KEYS | SIG_SCROLL)) {
      deadline = naos_millis() + SCR_IDLE_TIMEOUT;
    }

    // redraw on early timeouts to keep the app bar time and status current
    // also at slow sensor rates
    if (event.type == SIG_TIMEOUT && naos_millis() <= deadline) {
      continue;
    }

    // update on append or stop
    if (event.type & (SIG_SENSOR | SIG_APPEND | SIG_STOP)) {
      continue;
    }

    // handle idle timeout (at power-test level 3 the screen is kept active to
    // allow measuring its power consumption)
    if (event.type == SIG_TIMEOUT) {
      if (naos_get_l("power-test") >= 3) {
        deadline = naos_millis() + SCR_IDLE_TIMEOUT;
        continue;
      }

      // cleanup
      gui_cleanup(false);

      // set return
      scr_return_unlock = scr_view;

      return scr_idle;
    }

    // handle escape
    if (event.type == SIG_ESCAPE) {
      // handle precision mode
      if (precision) {
        precision = false;
        continue;
      }

      // cleanup
      gui_cleanup(false);

      // handle recording
      if (recording) {
        // choose option
        int ret = gui_choose(scr_trans()->exit__stop, scr_trans()->exit__back, true, SCR_ACTION_TIMEOUT);
        if (ret == 0) {
          return scr_view;
        }

        // set action, if not set (to not override action set by scr_create)
        if (scr_action == 0) {
          scr_action = STM_FROM_MEASUREMENT;
        }

        // handle stop
        if (ret == 1) {
          // stop recording
          rec_stop();

          // show message
          gui_message(lvx_fmt(scr_trans()->exit__stopped, scr_file_name(file)), SCR_MSG_TIMEOUT);

          // set action
          scr_action = STM_COMP_MEASUREMENT;
        }

        return scr_menu;
      }

      // set action
      scr_action = STM_FROM_ANALYSIS;

      // handle live
      if (scr_file == 0) {
        return scr_menu;
      }

      return scr_edit;
    }

    // handle enter key
    if (event.type == SIG_ENTER) {
      // add mark when recording
      if (recording) {
        rec_mark();
        continue;
      }

      // cancel advanced mode if too less
      if (source_count < LVX_CHART_SIZE) {
        gui_cleanup(false);
        gui_message(scr_trans()->view__not_enough, SCR_MSG_TIMEOUT);
        return scr_view;
      }

      // enter precision mode
      precision = true;

      continue;
    }

    // change field on up/down, down advances like in lists
    if (event.type == SIG_DOWN) {
      scr_field_cycle(true, has_pm);
      continue;
    } else if (event.type == SIG_UP) {
      scr_field_cycle(false, has_pm);
      continue;
    }

    // change position on left/right/scroll if not recording
    if (!recording) {
      if (event.type == SIG_LEFT) {
        position -= resolution * (event.repeat ? 5 : 1);
      } else if (event.type == SIG_RIGHT) {
        position += resolution * (event.repeat ? 5 : 1);
      } else if (event.type == SIG_SCROLL) {
        position += resolution * (int32_t)event.scroll.fast;
      }
      if (position > source_stop) {
        position = source_stop;
      }
      if (position < 0) {
        position = 0;
      }
    }
  }
}

static void* scr_create() {
  // get free samples
  uint32_t samples = rec_free(true);

  // handle no space
  if (!samples) {
    gui_message(scr_trans()->create__full, SCR_MSG_TIMEOUT);
    return scr_explore;
  }

  // check recording
  if (rec_running()) {
    gui_message(scr_trans()->recording, SCR_MSG_TIMEOUT);
    return scr_explore;
  }

  // get record rate
  int32_t record_rate = naos_get_l("record-rate");

  // calculate hours at record rate
  int32_t sph = 60 * 60 / record_rate;
  uint32_t hours = samples / sph;

  // confirm creation
  if (!gui_confirm(lvx_fmt(scr_trans()->create__info, dat_next(), hours), scr_trans()->create__start, scr_trans()->back,
                   false, 0)) {
    return scr_explore;
  }

  // confirm import
  bool import = gui_confirm(scr_trans()->create__import, scr_trans()->yes, scr_trans()->no, false, SCR_ACTION_TIMEOUT);

  // determine epoch
  int64_t epoch = al_clock_get_epoch();
  if (import) {
    al_sample_source_t source = al_store_source();
    epoch = source.start(source.ctx);
  }

  // create measurement
  scr_file = dat_create(epoch);

  // confirm and perform data import
  if (import) {
    // set flag
    hmi_set_flag(HMI_FLAG_PROCESS);

    // perform import
    gui_progress_start(scr_trans()->create__importing);
    dat_import(scr_file, 0, gui_progress_update);
    gui_cleanup(false);

    // clear flag
    hmi_clear_flag(HMI_FLAG_PROCESS);

    // write message
    gui_message(scr_trans()->create__imported, SCR_MSG_TIMEOUT);
  }

  // start recording
  rec_start(scr_file);

  // set action
  if (scr_file == 1) {
    scr_action = STM_START_FIRST_MEASUREMENT;
  } else {
    scr_action = STM_START_MEASUREMENT;
  }

  return scr_view;
}

static void* scr_edit() {
  // begin draw
  gfx_begin(false, false);

  // find file
  dat_file_t* file = dat_find(scr_file, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
    return NULL;
  }

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, scr_file_name(file));
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);

  // add info
  lv_obj_t* info = lv_label_create(lv_scr_act());
  lv_label_set_text(info, scr_file_info(file));
  lv_obj_align(info, LV_ALIGN_TOP_LEFT, 5, 26);

  // add signs
  lvx_sign_t analyze = {
      .title = "A",
      .text = scr_trans()->edit__analyse,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_t delete = {
      .title = "<",
      .text = scr_trans()->delete__delete,
      .align = LV_ALIGN_BOTTOM_LEFT,
      .offset = -25,
  };
  lvx_sign_t export = {
      .title = ">",
      .text = scr_trans()->edit__export,
      .align = LV_ALIGN_BOTTOM_RIGHT,
      .offset = -25,
  };
  lvx_sign_create(&analyze, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&delete, lv_scr_act());
  lvx_sign_create(&export, lv_scr_act());

  // end draw
  gfx_end(false, false);

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_META | SIG_LEFT | SIG_RIGHT, SCR_ACTION_TIMEOUT);

    // cleanup
    gui_cleanup(false);

    // handle delete
    if (event.type == SIG_LEFT) {
      // confirm deletion
      const char* msg = lvx_fmt(scr_trans()->delete__confirm, scr_file_name(file));
      if (!gui_confirm(msg, scr_trans()->delete__delete, scr_trans()->back, false, SCR_ACTION_TIMEOUT)) {
        return scr_edit;
      }

      // delete file
      uint16_t num = file->head.num;
      dat_delete(file->head.num);
      gui_message(lvx_fmt(scr_trans()->delete__deleted, num), SCR_MSG_TIMEOUT);

      // set action
      scr_action = STM_DEL_MEASUREMENT;

      return scr_explore;
    }

    // handle export
    if (event.type == SIG_RIGHT) {
      // set flag
      hmi_set_flag(HMI_FLAG_PROCESS);

      // perform export
      gui_progress_start(scr_trans()->edit__exporting);
      bool ok = dat_export(scr_file, gui_progress_update);
      gui_cleanup(false);

      // clear flag
      hmi_clear_flag(HMI_FLAG_PROCESS);

      // export file
      if (!ok) {
        gui_message(scr_trans()->edit__export_fail, SCR_MSG_TIMEOUT);
      } else {
        gui_message(scr_trans()->edit__export_done, SCR_MSG_TIMEOUT);
      }

      return scr_edit;
    }

    // handle event
    switch (event.type) {
      case SIG_ESCAPE:
      case SIG_TIMEOUT:
        return scr_explore;
      case SIG_ENTER:
        return scr_view;
      default:
        ESP_ERROR_CHECK(ESP_FAIL);
    }
  }
}

static gui_list_item_t scr_explore_cb(int num, void* ctx) {
  // handle create
  if (num == 0) {
    return (gui_list_item_t){
        .title = scr_trans()->explore__create,
        .info = "",
    };
  }

  // get file
  dat_file_t* file = dat_get(num - 1);

  return (gui_list_item_t){
      .title = scr_file_name(file),
      .info = scr_file_date(file),
  };
}

static void* scr_explore() {
  // prepare state
  static int offset = 0;

  // get total
  size_t total = dat_count();

  // ignore last if recording
  if (rec_running()) {
    total--;
  }

  // get start
  int start = 0;
  if (dat_find(scr_file, &start)) {
    start++;
  }

  // show list
  int selected = gui_list((int)total + 1, start, &offset, scr_trans()->explore__select, scr_trans()->back,
                          scr_explore_cb, NULL, SCR_ACTION_TIMEOUT);
  if (selected < 0) {
    return scr_menu;
  }

  // handle create
  if (selected == 0) {
    scr_file = 0;
    return scr_create;
  }

  // set file
  scr_file = dat_get(selected - 1)->head.num;

  return scr_edit;
}

static void* scr_usb() {
  // check recording
  if (rec_running()) {
    // show message
    gui_message(scr_trans()->recording, SCR_MSG_TIMEOUT);

    return scr_menu;
  }

  // check connection
  if (!al_power_get().has_usb) {
    // show message
    gui_message(scr_trans()->usb__disconnected, SCR_MSG_TIMEOUT);

    return scr_menu;
  }

  // set modal flag
  hmi_set_flag(HMI_FLAG_MODAL);

  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, scr_trans()->usb__active);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  // add signs
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // enable USB
  dat_enable_usb();

  // await escape
  sig_event_t event = sig_await(SIG_ESCAPE | SIG_EJECT, 0);

  // disable USB
  dat_disable_usb();

  // cleanup
  gui_cleanup(false);

  // clear modal flag
  hmi_clear_flag(HMI_FLAG_MODAL);

  // show message on eject
  if (event.type == SIG_EJECT) {
    gui_message(scr_trans()->usb__eject, SCR_MSG_TIMEOUT);
  }

  return scr_menu;
}

static void* scr_ble() {
  // wait for com to start
  while (!com_started()) {
    naos_delay(100);
  }

  // set modal flag
  hmi_set_flag(HMI_FLAG_MODAL);

  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, lvx_fmt(scr_trans()->ble__active, naos_get_s("device-name")));
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -8);

  // add signs
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // enable pairing
  naos_ble_enable_pairing();

  // await escape
  sig_await(SIG_ESCAPE, 0);

  // disable pairing
  naos_ble_disable_pairing();

  // cleanup
  gui_cleanup(false);

  // clear modal flag
  hmi_clear_flag(HMI_FLAG_MODAL);

  return scr_menu;
}

// The battery-life ladder and model come from the pwr module and are shared
// with AL Studio and the app (see the cross-surface decision register).
// There is deliberately no aggregate profile parameter: applying a rung
// writes the individual parameters, so any client stays compatible.

// the estimated runtime of the written configuration, in whole days
static int scr_power_days() {
  return (int)lround(pwr_days(naos_get_l("sleep-rate"), naos_get_l("display-rate"), naos_get_l("gas-window")));
}

// the rung matching the written configuration exactly, or -1 for a custom
// configuration (a non-default pm-rate also counts as custom)
static int scr_power_rung() {
  if (naos_get_l("pm-rate") != 0) {
    return -1;
  }
  int32_t sleep = naos_get_l("sleep-rate");
  int32_t display = naos_get_l("display-rate");
  int32_t gas = naos_get_l("gas-window");
  for (int i = 0; i < pwr_num_rungs(); i++) {
    pwr_rung_t rung = pwr_rung(i);
    if (rung.sleep == sleep && rung.display == display && rung.gas == gas) {
      return i;
    }
  }
  return -1;
}

// Interactive profile picker: left/right steps through the ladder while
// previewing the runtime and the parameters it would set; nothing is written
// until the selection is confirmed. A custom configuration is shown first and
// enters the ladder at the defaults rung on the first step.
static void scr_power_profile() {
  // get translation
  const scr_trans_t* t = scr_trans();

  // start from the matched rung, or the defaults rung for a custom
  // configuration
  int rung = scr_power_rung();
  bool moved = false;
  int sel = rung < 0 ? 1 : rung;

  // begin draw
  gfx_begin(false, false);

  // add value with step arrows
  lv_obj_t* value = lv_label_create(lv_scr_act());
  lv_obj_align(value, LV_ALIGN_TOP_MID, 0, 25);

  // add parameter preview
  lv_obj_t* info = lv_label_create(lv_scr_act());
  lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 58);
  lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_font(info, &fnt_8, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(info, 5, LV_PART_MAIN);

  // add signs
  lvx_sign_t save = {
      .title = "A",
      .text = t->save,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t cancel = {
      .title = "B",
      .text = t->cancel,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&save, lv_scr_act());
  lvx_sign_create(&cancel, lv_scr_act());

  // end draw
  gfx_end(true, false);

  for (;;) {
    // determine previewed configuration
    int32_t sleep, display, window;
    if (rung < 0 && !moved) {
      sleep = naos_get_l("sleep-rate");
      display = naos_get_l("display-rate");
      window = naos_get_l("gas-window");
    } else {
      pwr_rung_t preview = pwr_rung(sel);
      sleep = preview.sleep;
      display = preview.display;
      window = preview.gas;
    }

    // begin draw
    gfx_begin(false, false);

    // update value with the estimated runtime, marking an off-ladder
    // configuration as custom; the estimate is omitted while PM measurements
    // are enabled, as the model does not cover their cost
    const char* days = lvx_fmt(t->config__profile_days, (int)lround(pwr_days(sleep, display, window)));
    if (rung < 0 && !moved) {
      if (naos_get_l("pm-rate") != 0) {
        lv_label_set_text(value, lvx_fmt("< %s >", t->config__custom));
      } else {
        lv_label_set_text(value, lvx_fmt("< %s: %s >", t->config__custom, days));
      }
    } else {
      lv_label_set_text(value, lvx_fmt("< %s >", days));
    }

    // update parameter preview
    const char* gas = window > 0 ? lvx_fmt("%s %lds", t->config__gas_mode_duty, window)
                                 : (window == 0 ? t->config__gas_mode_cont : t->off);
    lv_label_set_text(info, lvx_fmt("%s: %lds\n%s: %lds\n%s: %s", t->config__sleep_rate, sleep,
                                    t->config__display_rate, display, t->config__gas_mode, gas));

    // end draw
    gfx_end(false, false);

    // await event
    sig_event_t event = sig_await(SIG_KEYS | SIG_SCROLL, SCR_ACTION_TIMEOUT);

    // handle steps
    if ((event.type & (SIG_ARROWS | SIG_SCROLL)) != 0) {
      int step;
      if (event.type == SIG_SCROLL) {
        step = (int)event.scroll.std;
      } else {
        step = (event.type == SIG_RIGHT || event.type == SIG_DOWN) ? 1 : -1;
      }
      sel += step;
      if (sel < 0) {
        sel = 0;
      } else if (sel > pwr_num_rungs() - 1) {
        sel = pwr_num_rungs() - 1;
      }
      if (step != 0) {
        moved = true;
      }
      continue;
    }

    // cleanup
    gui_cleanup(false);

    // apply the selection on confirmation, also resetting the PM rate so the
    // configuration matches the rung afterwards
    if (event.type == SIG_ENTER && moved) {
      pwr_rung_t chosen = pwr_rung(sel);
      naos_set_l("sleep-rate", chosen.sleep);
      naos_set_l("display-rate", chosen.display);
      naos_set_l("gas-window", chosen.gas);
      naos_set_l("pm-rate", 0);
    }

    return;
  }
}

// The config categories, mirroring the shared settings taxonomy (General,
// Measurement, Power, Connectivity, Advanced). Items reference the ids
// dispatched in scr_config_cb() and scr_config_items().
static const uint8_t scr_cat_general[] = {0, 1, 2, 3, 12};
static const uint8_t scr_cat_measure[] = {4, 11};
static const uint8_t scr_cat_power[] = {27, 5, 8, 6, 28, 9, 10, 7};
static const uint8_t scr_cat_connect[] = {16, 17, 18, 19, 20, 21, 22, 23};
static const uint8_t scr_cat_advanced[] = {14, 15, 13, 24, 25, 26};

typedef struct {
  const uint8_t* items;
  int num;
} scr_cat_t;

static const scr_cat_t scr_cats[] = {
    {scr_cat_general, (int)sizeof(scr_cat_general)},   {scr_cat_measure, (int)sizeof(scr_cat_measure)},
    {scr_cat_power, (int)sizeof(scr_cat_power)},       {scr_cat_connect, (int)sizeof(scr_cat_connect)},
    {scr_cat_advanced, (int)sizeof(scr_cat_advanced)},
};

#define SCR_CAT_NUM ((int)(sizeof(scr_cats) / sizeof(scr_cat_t)))

static int scr_config_cat = 0;

static gui_list_item_t scr_config_cb(int num, void* ctx) {
  // get translation
  const scr_trans_t* t = scr_trans();

  // handle config items
  switch (num) {
    case 0: {
      return (gui_list_item_t){
          .title = t->config__language,
          .info = scr_lang_str[scr_lang()],
      };
    }
    case 1: {
      // get date
      uint16_t year, month, day;
      al_clock_get_date(&year, &month, &day);

      return (gui_list_item_t){
          .title = t->config__date,
          .info = lvx_fmt("%d-%02d-%02d", year, month, day),
      };
    }
    case 2: {
      // get time
      uint16_t hour, minute, seconds;
      al_clock_get_time(&hour, &minute, &seconds);

      return (gui_list_item_t){
          .title = t->config__time,
          .info = lvx_fmt("%02d:%02d", hour, minute),
      };
    }
    case 3: {
      return (gui_list_item_t){
          .title = t->config__timezone,
          .info = naos_get_s("time-tz-name"),
      };
    }
    case 4: {
      return (gui_list_item_t){
          .title = t->config__main_rate,
          .info = lvx_fmt("%lds", naos_get_l("main-rate")),
      };
    }
    case 5: {
      return (gui_list_item_t){
          .title = t->config__sleep_rate,
          .info = lvx_fmt("%lds", naos_get_l("sleep-rate")),
      };
    }
    case 6: {
      return (gui_list_item_t){
          .title = t->config__record_rate,
          .info = lvx_fmt("%lds", naos_get_l("record-rate")),
      };
    }
    case 7: {
      // treat zero as off
      int32_t rate = naos_get_l("pm-rate");

      return (gui_list_item_t){
          .title = t->config__pm_rate,
          .info = rate == 0 ? t->off : lvx_fmt("%lds", rate),
      };
    }
    case 8: {
      return (gui_list_item_t){
          .title = t->config__display_rate,
          .info = lvx_fmt("%lds", naos_get_l("display-rate")),
      };
    }
    case 9: {
      // the window only applies while the gas sensor is duty cycled
      int32_t window = naos_get_l("gas-window");

      return (gui_list_item_t){
          .title = t->config__gas_window,
          .info = window > 0 ? lvx_fmt("%lds", window) : "-",
      };
    }
    case 10: {
      // treat zero as no grace
      int32_t grace = naos_get_l("gas-grace");

      return (gui_list_item_t){
          .title = t->config__gas_grace,
          .info = grace == 0 ? t->off : lvx_fmt("%lds", grace),
      };
    }
    case 11: {
      return (gui_list_item_t){
          .title = t->config__long_interval,
          .info = lvx_fmt("%lds", naos_get_l("long-interval")),
      };
    }
    case 12: {
      return (gui_list_item_t){
          .title = t->config__temp_unit,
          .info = naos_get_b("fahrenheit") ? "°F" : "°C",
      };
    }
    case 13: {
      return (gui_list_item_t){
          .title = t->config__developer,
          .info = naos_get_b("developer") ? t->on : t->off,
      };
    }
    case 14: {
      return (gui_list_item_t){
          .title = t->config__power_light,
          .info = naos_get_b("power-light") ? t->on : t->off,
      };
    }
    case 15: {
      return (gui_list_item_t){
          .title = t->config__co2_light,
          .info = naos_get_b("co2-light") ? t->on : t->off,
      };
    }
    case 16: {
      return (gui_list_item_t){
          .title = t->config__wifi_network,
          .info = lvx_truncate(naos_get_s("wifi-ssid"), 20),
      };
    }
    case 17: {
      return (gui_list_item_t){
          .title = "MQTT Broker",
          .info = lvx_truncate(naos_get_s("mqtt-host"), 20),
      };
    }
    case 18: {
      return (gui_list_item_t){
          .title = "Home Assistant",
          .info = naos_get_b("mqtt-ha") ? t->on : t->off,
      };
    }
    case 19: {
      return (gui_list_item_t){
          .title = t->config__ble_prev_sleep,
          .info = naos_get_b("ble-prev-sleep") ? t->on : t->off,
      };
    }
    case 20: {
      return (gui_list_item_t){
          .title = t->config__mqtt_prev_sleep,
          .info = naos_get_b("mqtt-prev-sleep") ? t->on : t->off,
      };
    }
    case 21: {
      return (gui_list_item_t){
          .title = t->config__ble_pairing,
          .info = naos_get_b("ble-pairing") ? t->on : t->off,
      };
    }
    case 22: {
      return (gui_list_item_t){
          .title = t->config__ble_bonding,
          .info = naos_get_b("ble-bonding") ? t->on : t->off,
      };
    }
    case 23: {
      return (gui_list_item_t){
          .title = t->config__ble_clear,
          .info = t->execute,
      };
    }
    case 24: {
      return (gui_list_item_t){
          .title = t->config__altitude,
          .info = lvx_fmt("%ldm", naos_get_l("altitude")),
      };
    }
    case 25: {
      return (gui_list_item_t){
          .title = t->config__clock_cal,
          .info = lvx_fmt("%dppm", naos_get_l("clock-cal")),
      };
    }
    case 26: {
      return (gui_list_item_t){
          .title = t->config__reset,
          .info = t->execute,
      };
    }
    case 27: {
      // show the estimated runtime of a matched rung or custom configuration;
      // the estimate is omitted while PM measurements are enabled, as the
      // model does not cover their cost
      int rung = scr_power_rung();
      const char* days = lvx_fmt(t->config__profile_days, scr_power_days());
      const char* info;
      if (rung >= 0) {
        info = days;
      } else if (naos_get_l("pm-rate") != 0) {
        info = t->config__custom;
      } else {
        info = lvx_fmt("%s: %s", t->config__custom, days);
      }

      return (gui_list_item_t){
          .title = t->config__profile,
          .info = info,
      };
    }
    case 28: {
      // derive the gas sensor mode from the window value
      int32_t window = naos_get_l("gas-window");

      return (gui_list_item_t){
          .title = t->config__gas_mode,
          .info = window < 0 ? t->off : (window == 0 ? t->config__gas_mode_cont : t->config__gas_mode_duty),
      };
    }
    default:
      ESP_ERROR_CHECK(ESP_FAIL);
      return (gui_list_item_t){0};
  }
}

static gui_list_item_t scr_config_item_cb(int num, void* ctx) {
  // delegate to the id-based renderer
  const scr_cat_t* cat = ctx;
  return scr_config_cb(cat->items[num], NULL);
}

static void* scr_config_items() {
  // prepare per-category state
  static int offsets[SCR_CAT_NUM] = {0};
  static int selection[SCR_CAT_NUM] = {0};

  // get translation and category
  const scr_trans_t* t = scr_trans();
  scr_cat_t cat = scr_cats[scr_config_cat];

  // hide the PM rate (7) on devices without a PM sensor
  uint8_t items[sizeof(scr_cat_power)];
  if (cat.items == scr_cat_power && !al_sensor_pm_present()) {
    cat.num = 0;
    for (int i = 0; i < (int)sizeof(scr_cat_power); i++) {
      if (scr_cat_power[i] != 7) {
        items[cat.num++] = scr_cat_power[i];
      }
    }
    cat.items = items;
  }

  for (;;) {
    // select parameter
    int choice = gui_list(cat.num, selection[scr_config_cat], &offsets[scr_config_cat], t->change, t->back,
                          scr_config_item_cb, (void*)&cat, SCR_ACTION_TIMEOUT);
    if (choice < 0) {
      return scr_config;
    }

    // store choice
    selection[scr_config_cat] = choice;

    // handle choice
    switch (cat.items[choice]) {
      case 0: {
        // cycle through the available languages
        scr_lang_t lang = scr_lang();
        if (lang == SCR_DE) {
          naos_set_s("language", "en");
        } else if (lang == SCR_EN) {
          naos_set_s("language", "es");
        } else if (lang == SCR_ES) {
          naos_set_s("language", "fr");
        } else {
          naos_set_s("language", "de");
        }

        // reload screen
        return scr_config_items;
      }

      case 1: {
        // check recording
        if (rec_running()) {
          gui_message(scr_trans()->recording, SCR_MSG_TIMEOUT);
          return scr_config_items;
        }

        // change date
        scr_date();

        break;
      }

      case 2: {
        // check recording
        if (rec_running()) {
          gui_message(scr_trans()->recording, SCR_MSG_TIMEOUT);
          return scr_config_items;
        }

        // change time
        scr_time();

        break;
      }

      case 4: {
        // cycle through sensor rates
        int32_t value = naos_get_l("main-rate");
        if (value == 5) {
          naos_set_l("main-rate", 30);
        } else if (value == 30) {
          naos_set_l("main-rate", 60);
        } else if (value == 60) {
          naos_set_l("main-rate", 120);
        } else if (value == 120) {
          naos_set_l("main-rate", 300);
        } else {
          naos_set_l("main-rate", 5);
        }

        break;
      }

      case 5: {
        // cycle through the canonical sleep rates
        int32_t value = naos_get_l("sleep-rate");
        if (value == 5) {
          naos_set_l("sleep-rate", 15);
        } else if (value == 15) {
          naos_set_l("sleep-rate", 30);
        } else if (value == 30) {
          naos_set_l("sleep-rate", 60);
        } else if (value == 60) {
          naos_set_l("sleep-rate", 120);
        } else if (value == 120) {
          naos_set_l("sleep-rate", 180);
        } else if (value == 180) {
          naos_set_l("sleep-rate", 300);
        } else if (value == 300) {
          naos_set_l("sleep-rate", 600);
        } else {
          naos_set_l("sleep-rate", 5);
        }

        break;
      }

      case 6: {
        // cycle through sensor rates
        int32_t value = naos_get_l("record-rate");
        if (value == 5) {
          naos_set_l("record-rate", 30);
        } else if (value == 30) {
          naos_set_l("record-rate", 60);
        } else if (value == 60) {
          naos_set_l("record-rate", 120);
        } else if (value == 120) {
          naos_set_l("record-rate", 300);
        } else {
          naos_set_l("record-rate", 5);
        }

        break;
      }

      case 7: {
        // cycle through PM rates, zero disables PM measurements while dozing
        int32_t value = naos_get_l("pm-rate");
        if (value == 0) {
          naos_set_l("pm-rate", 30);
        } else if (value == 30) {
          naos_set_l("pm-rate", 60);
        } else if (value == 60) {
          naos_set_l("pm-rate", 120);
        } else if (value == 120) {
          naos_set_l("pm-rate", 300);
        } else {
          naos_set_l("pm-rate", 0);
        }

        break;
      }

      case 8: {
        // cycle through display intervals within the clamped bounds
        int32_t value = naos_get_l("display-rate");
        if (value == 60) {
          naos_set_l("display-rate", 120);
        } else if (value == 120) {
          naos_set_l("display-rate", 180);
        } else if (value == 180) {
          naos_set_l("display-rate", 300);
        } else {
          naos_set_l("display-rate", 60);
        }

        break;
      }

      case 9: {
        // adjust the duty-cycle window within the firmware's clamp of at
        // least 10s and at most half the sleep interval
        int32_t window = naos_get_l("gas-window");
        if (window <= 0) {
          gui_message(t->config__gas_window_hint, SCR_MSG_TIMEOUT);
          break;
        }
        int max = (int)(naos_get_l("sleep-rate") / 2);
        if (max < 10) {
          max = 10;
        }
        int value = (int)window;
        if (value < 10) {
          value = 10;
        } else if (value > max) {
          value = max;
        }
        if (gui_wheel(t->config__gas_window, &value, 10, 5, max, t->save, t->cancel, "%lds", SCR_ACTION_TIMEOUT)) {
          naos_set_l("gas-window", value);
        }

        break;
      }

      case 10: {
        // cycle through the canonical grace periods, zero applies the gas
        // window immediately
        int32_t value = naos_get_l("gas-grace");
        if (value == 0) {
          naos_set_l("gas-grace", 300);
        } else if (value == 300) {
          naos_set_l("gas-grace", 900);
        } else if (value == 900) {
          naos_set_l("gas-grace", 1800);
        } else if (value == 1800) {
          naos_set_l("gas-grace", 3600);
        } else if (value == 3600) {
          naos_set_l("gas-grace", 7200);
        } else {
          naos_set_l("gas-grace", 0);
        }

        break;
      }

      case 11: {
        // use wheel to change long interval
        int value = naos_get_l("long-interval");
        if (gui_wheel(t->config__long_interval, &value, 30, 10, 900, t->save, t->cancel, "%lds", SCR_ACTION_TIMEOUT)) {
          naos_set_l("long-interval", value);
        }

        break;
      }

      case 12: {
        // toggle fahrenheit temperature setting
        naos_set_b("fahrenheit", !naos_get_b("fahrenheit"));

        break;
      }

      case 13: {
        // toggle developer mode
        naos_set_b("developer", !naos_get_b("developer"));

        break;
      }

      case 14: {
        // toggle power light
        naos_set_b("power-light", !naos_get_b("power-light"));

        break;
      }

      case 15: {
        // toggle CO2 light
        naos_set_b("co2-light", !naos_get_b("co2-light"));

        break;
      }

      case 19: {
        // toggle BLE no sleep
        naos_set_b("ble-prev-sleep", !naos_get_b("ble-prev-sleep"));

        break;
      }

      case 20: {
        // toggle MQTT no sleep
        naos_set_b("mqtt-prev-sleep", !naos_get_b("mqtt-prev-sleep"));

        break;
      }

      case 21: {
        // toggle BLE pairing
        bool value = !naos_get_b("ble-pairing");
        naos_set_b("ble-pairing", value);
        if (gui_confirm(lvx_fmt("Pairing: %s\n\nRestart now?", value ? "ON" : "OFF"), scr_trans()->yes, scr_trans()->no,
                        false, SCR_ACTION_TIMEOUT)) {
          naos_reboot();
        }

        break;
      }

      case 22: {
        // toggle BLE bonding
        bool value = !naos_get_b("ble-bonding");
        naos_set_b("ble-bonding", value);
        if (gui_confirm(lvx_fmt("Bonding: %s\n\nRestart now?", value ? "ON" : "OFF"), scr_trans()->yes, scr_trans()->no,
                        false, SCR_ACTION_TIMEOUT)) {
          naos_reboot();
        }

        break;
      }

      case 23: {
        // clear BLE peers
        naos_ble_peerlist_clear();
        naos_ble_allowlist_clear();
        gui_message(scr_trans()->config__ble_cleared, SCR_MSG_TIMEOUT);

        break;
      }

      case 24: {
        // use wheel to change altitude
        int value = naos_get_l("altitude");
        if (gui_wheel(t->config__altitude, &value, -500, 10, 5000, t->save, t->cancel, "%dm", SCR_ACTION_TIMEOUT)) {
          naos_set_l("altitude", value);
        }

        break;
      }

      case 25: {
        // use wheel to change clock calibration
        int value = naos_get_l("clock-cal");
        if (gui_wheel(t->config__clock_cal, &value, -63, 1, 126, t->save, t->cancel, "%dppm", SCR_ACTION_TIMEOUT)) {
          naos_set_l("clock-cal", value);
        }

        break;
      }

      case 26: {
        // check recording
        if (rec_running()) {
          gui_message(scr_trans()->recording, SCR_MSG_TIMEOUT);
          return scr_config_items;
        }

        // confirm reset
        if (!gui_confirm(scr_trans()->reset__confirm, scr_trans()->yes, scr_trans()->no, true, SCR_ACTION_TIMEOUT)) {
          return scr_config_items;
        }

        // reset data
        dat_reset();

        // reset settings
        naos_reset();

        // reset BLE lists
        naos_ble_peerlist_clear();
        naos_ble_allowlist_clear();

        // show message
        gui_message(scr_trans()->reset__reset, SCR_MSG_TIMEOUT);

        // restart device
        naos_reboot();

        break;
      }

      case 27: {
        // pick a rung on the profile screen
        scr_power_profile();

        break;
      }

      case 28: {
        // cycle the gas sensor mode; duty cycling starts at half the sleep
        // interval, the mildest window the firmware accepts
        int32_t window = naos_get_l("gas-window");
        if (window == 0) {
          int32_t start = naos_get_l("sleep-rate") / 2;
          naos_set_l("gas-window", start < 10 ? 10 : start);
        } else if (window > 0) {
          naos_set_l("gas-window", -1);
        } else {
          naos_set_l("gas-window", 0);
        }

        break;
      }

      default:
        // show read-only message for AL Studio managed settings
        gui_message(t->config__studio, SCR_ACTION_TIMEOUT);
    }
  }
}

static gui_list_item_t scr_config_cat_cb(int num, void* ctx) {
  // get translation
  const scr_trans_t* t = scr_trans();

  // handle categories
  switch (num) {
    case 0:
      return (gui_list_item_t){.title = t->config__general, .info = ""};
    case 1:
      return (gui_list_item_t){.title = t->config__measurement, .info = ""};
    case 2:
      return (gui_list_item_t){.title = t->config__power, .info = ""};
    case 3:
      return (gui_list_item_t){.title = t->config__connectivity, .info = ""};
    case 4:
      return (gui_list_item_t){.title = t->config__advanced, .info = ""};
    default:
      ESP_ERROR_CHECK(ESP_FAIL);
      return (gui_list_item_t){0};
  }
}

static void* scr_config() {
  // prepare state
  static int offset = 0;
  static int selected = 0;

  // get translation
  const scr_trans_t* t = scr_trans();

  // select category
  int choice = gui_list(SCR_CAT_NUM, selected, &offset, t->explore__select, t->back, scr_config_cat_cb, NULL,
                        SCR_ACTION_TIMEOUT);
  if (choice < 0) {
    return scr_settings;
  }

  // store choice
  selected = choice;
  scr_config_cat = choice;

  return scr_config_items;
}

static void* scr_check() {
  // date
  uint16_t year;
  if (!al_clock_verify(&year) || year < 2026) {
    gui_message("Date check failed!", SCR_MSG_TIMEOUT);
    return scr_develop;
  }

  // buttons
  gui_write("Press all buttons once...", false);
  sig_type_t pressed = 0;
  while ((pressed & SIG_KEYS) != SIG_KEYS) {
    pressed |= sig_await(SIG_KEYS, 0).type;
  }

  // accel
  gui_cleanup(false);
  gui_write("Rotate the device...", false);
  for (;;) {
    sig_await(SIG_MOTION, 0);
    al_accel_state_t state = al_accel_get();
    if (state.front && state.rotation != 0) {
      break;
    }
  }

  // touch
  gui_cleanup(false);
  gui_write("Scroll the touch strip...", false);
  for (;;) {
    sig_event_t event = sig_await(SIG_SCROLL, 0);
    if (event.scroll.std >= 2 || event.scroll.std <= -2) {
      break;
    }
  }

  // power
  gui_cleanup(false);
  gui_write("Plug in USB power...", false);
  for (;;) {
    sig_await(SIG_POWER, 0);
    if (al_power_get().has_usb) {
      break;
    }
  }

  // buzzer
  gui_cleanup(false);
  gui_write("Listen for the buzzer...", false);
  al_buzzer_beep(4400, 200, true);
  naos_delay(1000);
  al_buzzer_beep(440, 200, true);
  naos_delay(1000);

  // LED
  gui_cleanup(false);
  gui_write("Check the LED colors...", false);
  hmi_set_flag(HMI_FLAG_IGNORE);
  naos_delay(250);
  al_led_set(255, 0, 0);
  naos_delay(1000);
  al_led_set(0, 255, 0);
  naos_delay(1000);
  al_led_set(0, 0, 255);
  naos_delay(1000);
  al_led_set(0, 0, 0);
  hmi_clear_flag(HMI_FLAG_IGNORE);

  // sensors
  for (;;) {
    gui_cleanup(false);
    al_sample_t sample = al_store_last();
    float co2 = al_sample_read(sample, AL_SAMPLE_CO2);
    float tmp = al_sample_read(sample, AL_SAMPLE_TMP);
    float hum = al_sample_read(sample, AL_SAMPLE_HUM);
    float voc = al_sample_read(sample, AL_SAMPLE_VOC);
    gui_write(
        lvx_fmt("Blow on the sensors...\nCO2: %.0f/2500, TMP: %.1f/26\nHUM: %.0f/60, VOC: %.0f/50", co2, tmp, hum, voc),
        false);
    sig_await(SIG_SENSOR, 0);
    al_sample_t state = al_store_last();
    if (state.co2 > 2500 && state.tmp > 25 && state.hum > 60 && (state.voc & AL_SAMPLE_GAS_VALUE) > 50) {
      break;
    }
  }

  // cleanup
  gui_cleanup(false);

  return scr_develop;
}

static gui_list_item_t scr_about_cb(int num, void* ctx) {
  // get translation
  const scr_trans_t* t = scr_trans();

  // handle config items
  switch (num) {
    case 0: {
      return (gui_list_item_t){
          .title = t->about__device_name,
          .info = naos_get_s("device-name"),
      };
    }
    case 1: {
      // get authentication data
      naos_auth_data_t auth = {0};
      naos_auth_describe(&auth);

      return (gui_list_item_t){
          .title = t->about__serial_number,
          .info = lvx_fmt("NA-AL1-R%d/%d", auth.revision, auth.batch),
      };
    }
    case 2: {
      // find "g" in version
      int n = (int)(strchr(naos_config()->app_version, 'g') - naos_config()->app_version) - 1;

      return (gui_list_item_t){
          .title = t->about__firmware_version,
          .info = lvx_fmt("%.*s", n, naos_config()->app_version),
      };
    }
    case 3: {
      // get info
      al_storage_info_t info = al_storage_info(AL_STORAGE_INT);

      return (gui_list_item_t){
          .title = t->about__internal_storage,
          .info = lvx_fmt("%.2f %% of %.1f MB", info.usage * 100.f, (float)info.total / 1024.f / 1024.f),
      };
    }
    case 4: {
      // get info
      al_storage_info_t info = al_storage_info(AL_STORAGE_EXT);

      return (gui_list_item_t){
          .title = t->about__external_storage,
          .info = lvx_fmt("%.2f %% of %.1f MB", info.usage * 100.f, (float)info.total / 1024.f / 1024.f),
      };
    }
    default:
      ESP_ERROR_CHECK(ESP_FAIL);
      return (gui_list_item_t){0};
  }
}

static void* scr_about() {
  // prepare state
  static int offset = 0;
  static int selected = 0;

  // get translation
  const scr_trans_t* t = scr_trans();

  for (;;) {
    // select parameter
    int choice = gui_list(5, selected, &offset, NULL, t->back, scr_about_cb, NULL, SCR_ACTION_TIMEOUT);
    if (choice < 0) {
      return scr_settings;
    }

    // store choice
    selected = choice;
  }
}

static void* scr_settings() {
  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, scr_trans()->settings__title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);

  // add signs
  lvx_sign_t about = {
      .title = "A",
      .text = scr_trans()->settings__about,
      .align = LV_ALIGN_BOTTOM_LEFT,
      .offset = -25,
  };
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_t regulatory = {
      .title = "↑",
      .text = scr_trans()->settings__regulatory,
      .align = LV_ALIGN_BOTTOM_RIGHT,
      .offset = -75,
  };
  lvx_sign_t info = {
      .title = "<",
      .text = scr_trans()->settings__introduction,
      .align = LV_ALIGN_BOTTOM_RIGHT,
      .offset = -50,
  };
  lvx_sign_t off = {
      .title = ">",
      .text = scr_trans()->settings__config,
      .align = LV_ALIGN_BOTTOM_RIGHT,
      .offset = -25,
  };
  lvx_sign_t config = {
      .title = "↓",
      .text = scr_trans()->settings__off,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_create(&about, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&regulatory, lv_scr_act());
  lvx_sign_create(&info, lv_scr_act());
  lvx_sign_create(&off, lv_scr_act());
  lvx_sign_create(&config, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // await event
  sig_event_t event = sig_await(SIG_KEYS, SCR_ACTION_TIMEOUT);

  // cleanup
  gui_cleanup(false);

  // handle power off
  if (event.type == SIG_DOWN) {
    // check recording
    if (rec_running()) {
      gui_message(scr_trans()->recording, SCR_MSG_TIMEOUT);
      return scr_settings;
    }

    // turn off
    scr_power_off(false, true);

    return scr_settings;
  }

  // handle regulatory
  if (event.type == SIG_UP) {
    // prepare texts
    const char* texts[] = {
        "Product Information\n\nAir Lab - Portable Air Quality Monitor\nNA-AL1 / Made in Switzerland\n© 2025 Networked "
        "Artifacts Inc.\nhttps://networkedartifacts.com/airlab",
        "EU Regulations\n\nThis product complies with the following directives:\n2014/53/EU (RED)\n2011/65/EU (RoHS)",
        "FCC Statement 1/2\n\nThis device is certified under FCC ID 2BTBB-NA-AL1.\nThis device complies with Part 15 "
        "of the FCC rules.",
        "FCC Statement 2/2\n\nOperation is subject to the following two conditions:\n1. This device may not cause "
        "harmful interference; and\n2. This device must accept any interference received,\nincluding interference that "
        "may cause undesired operation.",
        NULL,
    };

    // show regulatory info
    gui_cycle(true, texts, scr_trans()->next, scr_trans()->back);

    return scr_settings;
  }

  // handle introduction
  if (event.type == SIG_LEFT) {
    // show introduction info
    gui_cycle(false, scr_trans()->intro__infos, scr_trans()->next, scr_trans()->back);

    return scr_settings;
  }

  // handle event
  switch (event.type) {
    case SIG_RIGHT:
      return scr_config;
    case SIG_ENTER:
      return scr_about;
    case SIG_ESCAPE:
    case SIG_TIMEOUT:
      // set action
      scr_action = STM_FROM_SETTINGS;

      return scr_menu;
    default:
      ESP_ERROR_CHECK(ESP_FAIL);
  }

  return scr_settings;
}

static gui_list_item_t scr_engine_cb(int num, void* _) {
  return (gui_list_item_t){
      .title = eng_get(num)->title,
      .info = eng_get(num)->version,
  };
}

static void* scr_engine() {
  // prepare state
  static int selected = 0;
  static int offset = 0;

  // reload engine
  eng_reload();

  // get count
  int count = (int)eng_num();

  // check count
  if (count == 0) {
    gui_message(scr_trans()->engine__empty, SCR_MSG_TIMEOUT);
    return scr_menu;
  }

  for (;;) {
    // select plugin
    selected = gui_list(count, selected, &offset, scr_trans()->engine__run, scr_trans()->back, scr_engine_cb, NULL,
                        SCR_ACTION_TIMEOUT);
    if (selected < 0) {
      return scr_menu;
    }

    // launch plugin
    eng_plugin_t* plugin = eng_get(selected);
    scr_launch(plugin->file, plugin->has_main ? "main" : "screen");
  }
}

static void* scr_develop() {
  // prepare variables
  static int selected = 0;
  static int offset = 0;

  // prepare labels
  const char* labels[] = {
      "System Info",   "Self Check", "Shipping Mode", "Sensor Data", "Sleep Mode", "CPU Reset",  "Power Off",
      "Clear Display", "Touch Info", "Compensation",  "Buzzer",      "PM Sensor",  "Power Test", NULL,
  };

  for (;;) {
    // select item
    selected = gui_list_strings(selected, &offset, labels, "Select", "Cancel", SCR_ACTION_TIMEOUT);
    if (selected < 0) {
      return scr_menu;
    }

    // handle system info
    if (selected == 0) {
      return scr_info;
    }

    // handle device check
    if (selected == 1) {
      return scr_check;
    }

    // handle ship mode
    if (selected == 2) {
      // disable developer mode
      naos_set_b("developer", false);

      // begin draw
      gfx_begin(false, false);

      // show image
      lv_obj_t* img = lv_img_create(lv_scr_act());
      lv_img_set_src(img, &img_shipping_mode);
      lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 20);

      // show message
      lv_obj_t* lbl = lv_label_create(lv_scr_act());
      lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -20);
      lv_label_set_text(lbl, "Yo! Plug in a USB-C cable,\nand press <A> to begin.");
      lv_obj_set_style_text_line_space(lbl, 6, LV_PART_MAIN);
      lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

      // end draw
      gfx_end(false, false);

      // show message
      naos_delay(1000);

      // enable ship mode
      al_power_ship();

      // clean up in case ship mode did not work
      gui_cleanup(false);
    }

    // handle sensor data
    if (selected == 3) {
      return scr_sensor;
    }

    // handle sleep
    if (selected == 4) {
      // log sleep
      naos_log("sleeping...");

      // write message
      gui_write("Deep Sleeping...\nPress <A> to wake up.", true);

      // perform sleep, without the ULP to reach the static floor (no return)
      al_sleep(false, 0);
    }

    // handle power reset
    if (selected == 5) {
      naos_reboot();
    }

    // handle power off
    if (selected == 6) {
      scr_power_off(false, false);
    }

    // handle clear display
    if (selected == 7) {
      gui_cleanup(true);
    }

    // handle touch info
    if (selected == 8) {
      // prepare data
      float position = NAN;
      float scroll = 0;
      float scroll_fast = 0;

      for (;;) {
        // update screen
        gui_write(lvx_fmt("Position: %.1f\nScroll: %.1f, %.1f", position, scroll, scroll_fast), false);

        // await event
        sig_event_t event = sig_await(SIG_ESCAPE | SIG_TOUCH | SIG_SCROLL, 0);

        // cleanup
        gui_cleanup(false);

        // handle events
        if (event.type & SIG_ESCAPE) {
          break;
        } else if (event.type & SIG_TOUCH) {
          position = event.position;
        } else if (event.type & SIG_SCROLL) {
          scroll = event.scroll.std;
          scroll_fast = event.scroll.fast;
        }
      }
    }

    // handle compensation
    if (selected == 9) {
      // prepare variables
      static const int32_t intervals[] = {5, 30, 60, 120, 300};
      size_t interval = 0;

      for (;;) {
        // get last sample
        al_sample_t sample = al_store_last();
        float tmp = al_sample_read(sample, AL_SAMPLE_TMP);
        float hum = al_sample_read(sample, AL_SAMPLE_HUM);

        // get charger phase
        static const char* phase_names[] = {
            [AL_POWER_PHASE_NONE] = "None", [AL_POWER_PHASE_USB] = "USB",   [AL_POWER_PHASE_PRE] = "Pre",
            [AL_POWER_PHASE_FAST] = "Fast", [AL_POWER_PHASE_TERM] = "Term",
        };
        const char* phase = phase_names[al_power_get().phase];

        // update screen
        gui_write(lvx_fmt("Rate: %lds\nTemp: %.1f\nHum: %.1f\nPhase: %s", intervals[interval], tmp, hum, phase), false);

        // await event
        sig_event_t event = sig_await(SIG_SENSOR | SIG_ESCAPE | SIG_ENTER, 0);

        // cleanup
        gui_cleanup(false);

        // handle events
        if (event.type & SIG_ESCAPE) {
          break;
        } else if (event.type & SIG_ENTER) {
          interval++;
          if (interval >= sizeof(intervals) / sizeof(intervals[0])) {
            interval = 0;
          }
          al_sensor_set_interval(intervals[interval]);
        }
      }
    }

    // handle buzzer
    if (selected == 10) {
      // begin draw
      gfx_begin(false, false);

      // add row
      lv_obj_t* row = lv_obj_create(lv_scr_act());
      lv_obj_set_size(row, 200, 100);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_align(row, LV_ALIGN_CENTER, 0, 0);
      lv_obj_set_style_pad_row(row, 5, LV_PART_MAIN);
      lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);

      // prepare wheels
      lvx_wheel_t hertz = {.value = 2000, .min = 0, .step = 50, .max = 8000, .format = "%04d", .fixed = true};
      lvx_wheel_t duration = {.value = 100, .min = 0, .step = 100, .max = 5000, .format = "%04d", .fixed = true};
      lvx_wheel_create(&hertz, row);
      lvx_wheel_create(&duration, row);

      // add button
      lvx_sign_t back = {
          .title = "B",
          .text = "Exit",
          .align = LV_ALIGN_BOTTOM_LEFT,
      };
      lvx_sign_t next = {
          .title = "A",
          .text = "Play",
          .align = LV_ALIGN_BOTTOM_RIGHT,
      };
      lvx_sign_create(&back, lv_scr_act());
      lvx_sign_create(&next, lv_scr_act());

      // focus first wheel
      lvx_wheel_focus(&hertz, true);

      // end draw
      gfx_end(false, false);

      // prepare wheels
      lvx_wheel_t* wheels[] = {&hertz, &duration};
      int cur_wheel = 0;

      for (;;) {
        // await event
        sig_event_t event = sig_await(SIG_KEYS | SIG_SCROLL, 0);

        // apply wheel events
        if (event.type & (SIG_ARROWS | SIG_SCROLL)) {
          gfx_begin(false, false);
          lvx_wheel_group_update(wheels, 2, event, &cur_wheel);
          gfx_end(false, false);
          continue;
        }

        // handle enter
        if (event.type == SIG_ENTER) {
          // play beep
          al_buzzer_beep(hertz.value, duration.value, false);

          continue;
        }

        // cleanup
        gui_cleanup(false);

        break;
      }
    }

    // handle PM sensor
    if (selected == 11) {
      // skip if no chip was detected
      if (!al_sensor_pm_present()) {
        gui_message("No PM sensor detected!", SCR_MSG_TIMEOUT);
        continue;
      }

      // prepare rates and mode, starting in manual mode so measurements are
      // taken on request instead of by the sensor monitor
      static const int32_t rates[] = {0, 30, 60};
      size_t rate = 0;
      bool manual = true;

      // apply the rate and mode
      al_sensor_set_pm_rate(rates[rate], manual);

      // begin draw
      gfx_begin(false, false);

      // add label
      lv_obj_t* label = lv_label_create(lv_scr_act());
      lv_obj_align(label, LV_ALIGN_CENTER, 0, -20);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      lv_obj_set_style_text_line_space(label, 6, LV_PART_MAIN);

      // add signs
      lvx_sign_t exit = {
          .title = "B",
          .text = "Exit",
          .align = LV_ALIGN_BOTTOM_LEFT,
      };
      lvx_sign_t cycle = {
          .title = "↓",
          .text = "Rate",
          .align = LV_ALIGN_BOTTOM_LEFT,
          .offset = -25,
      };
      lvx_sign_t measure = {
          .title = "A",
          .text = "Measure",
          .align = LV_ALIGN_BOTTOM_RIGHT,
      };
      lvx_sign_t mode = {
          .title = ">",
          .text = "Mode",
          .align = LV_ALIGN_BOTTOM_RIGHT,
          .offset = -25,
      };
      lvx_sign_create(&exit, lv_scr_act());
      lvx_sign_create(&cycle, lv_scr_act());
      lvx_sign_create(&measure, lv_scr_act());
      lvx_sign_create(&mode, lv_scr_act());

      // end draw
      gfx_end(true, false);

      for (;;) {
        // read PM value and flags of the last sample
        al_sample_t last = al_store_last();
        float pm = al_sample_read(last, AL_SAMPLE_PM);
        bool obstructed = (al_sample_flags(last, AL_SAMPLE_PM) & AL_SAMPLE_PM_OBSTRUCTED) != 0;

        // prepare texts
        int32_t due = al_sensor_pm_due();
        int32_t age = al_sensor_pm_age();
        const char* pm_str = isnan(pm) ? "n/a" : lvx_fmt("%.1f µg/m3%s", pm, obstructed ? " (obstructed)" : "");
        const char* due_str = due == INT32_MAX ? "n/a" : lvx_fmt("%lds", due);
        const char* age_str = age == INT32_MAX ? "n/a" : lvx_fmt("%lds", age);

        // update label
        gfx_begin(false, false);
        lv_label_set_text(label, lvx_fmt("Rate: %lds (%s)\nDue: %s, Age: %s\nPM: %s", rates[rate],
                                         manual ? "manual" : "auto", due_str, age_str, pm_str));
        gfx_end(false, false);

        // await event
        sig_event_t event = sig_await(SIG_KEYS, 1000);

        // handle events
        if (event.type & SIG_ESCAPE) {
          break;
        } else if (event.type & SIG_ENTER) {
          // reject in automatic mode, as the sensor monitor re-applies the
          // rate and the burst would be discarded
          if (!manual) {
            gfx_begin(false, false);
            lv_label_set_text(label, "Manual mode only!");
            gfx_end(false, false);
            naos_delay(1000);
            continue;
          }

          // take a measurement, as done before deep sleep, which blocks for
          // the burst duration
          gfx_begin(false, false);
          lv_label_set_text(label, "Measuring...");
          gfx_end(false, false);
          al_sensor_pm_measure();
        } else if (event.type & (SIG_UP | SIG_DOWN)) {
          rate++;
          if (rate >= sizeof(rates) / sizeof(rates[0])) {
            rate = 0;
          }
          al_sensor_set_pm_rate(rates[rate], manual);
        } else if (event.type & (SIG_LEFT | SIG_RIGHT)) {
          manual = !manual;
          al_sensor_set_pm_rate(rates[rate], manual);
        }
      }

      // cleanup
      gui_cleanup(false);

      // restore the awake rate, the screen is only reachable while awake
      al_sensor_set_pm_rate(naos_get_l("main-rate"), false);
    }

    // handle power test
    if (selected == 12) {
      // use wheel to change power test level (0: off, 1: hi-Z, 2: + stay
      // awake, 3: + keep screen)
      int value = naos_get_l("power-test");
      if (gui_wheel("Power Test", &value, 0, 1, 3, "Save", "Cancel", "%d", SCR_ACTION_TIMEOUT)) {
        naos_set_l("power-test", value);
      }
    }
  }
}

static void* scr_menu() {
  // prepare variables
  static int8_t opt = 0;  // create, explore, settings, usb, ble, plugins, develop
  static bool fan_alt = false;

  // get settings
  bool developer = naos_get_b("developer");

  // begin draw
  gfx_begin(false, false);

  // add bar
  lvx_bar_t bar = {0};
  lvx_bar_create(&bar, lv_scr_act());

  // add line
  lv_obj_t* line = lv_obj_create(lv_scr_act());
  lv_obj_align(line, LV_ALIGN_BOTTOM_LEFT, 0, -8);
  lv_obj_set_width(line, lv_pct(100));
  lv_obj_set_height(line, 2);
  lv_obj_set_style_border_width(line, 2, LV_PART_MAIN);
  lv_obj_set_style_border_side(line, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
  lv_obj_set_style_border_color(line, lv_color_black(), LV_PART_MAIN);

  // add robin
  lv_obj_t* robin = lv_img_create(lv_scr_act());
  lv_img_set_src(robin, &img_robin_standing);
  lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);

  // add lab
  lv_obj_t* lab = lv_img_create(lv_scr_act());
  lv_img_set_src(lab, &img_lab);
  lv_obj_align(lab, LV_ALIGN_BOTTOM_RIGHT, -5, -10);

  // add icon
  lv_obj_t* icon = lv_img_create(lv_scr_act());
  lv_obj_align(icon, LV_ALIGN_BOTTOM_MID, 1, -38);

  // add fan
  lv_obj_t* fan = lv_img_create(lv_scr_act());
  lv_obj_align(fan, LV_ALIGN_BOTTOM_RIGHT, -19, -35);

  // add chart
  lv_obj_t* chart = lv_canvas_create(lv_scr_act());
  lv_color_t chart_buffer[LV_CANVAS_BUF_SIZE_TRUE_COLOR(24, 16)] = {0};
  lv_canvas_set_buffer(chart, chart_buffer, 24, 16, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(chart, LV_ALIGN_BOTTOM_RIGHT, -87, -53);
  lv_canvas_fill_bg(chart, lv_color_white(), LV_OPA_COVER);

  // add drain
  lv_obj_t* drain = lv_canvas_create(lv_scr_act());
  lv_color_t drain_buffer[LV_CANVAS_BUF_SIZE_TRUE_COLOR(22, 11)] = {0};
  lv_canvas_set_buffer(drain, drain_buffer, 22, 11, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(drain, LV_ALIGN_BOTTOM_RIGHT, -21, -71);
  lv_canvas_fill_bg(drain, lv_color_white(), LV_OPA_COVER);

  // add bubble
  lvx_bubble_t bubble = {};
  lvx_bubble_create(&bubble, lv_scr_act());

  // end draw
  gfx_end(true, false);

  // prepare deadline
  int64_t deadline = naos_millis() + SCR_IDLE_TIMEOUT;

  // prepare flags
  bool urgent = true;
  bool fun = false;

  // prepare statement
  stm_entry_t* statement = NULL;

  // prepare sample source
  al_sample_source_t source = al_store_source();

  for (;;) {
    // get time
    uint16_t hour, minute, seconds;
    al_clock_get_time(&hour, &minute, &seconds);

    // get last sample
    al_sample_t sample = al_store_last();

    // determine PM availability
    bool has_pm = al_sensor_pm_present() || sample.pm >= 0;
    scr_field_check(has_pm);

    // query sensor
    float values[SCR_HIST_POINTS] = {0};
    float min = 0, max = 0;
    al_sample_pick(&source, (al_sample_field_t)scr_field, SCR_HIST_POINTS, values, &min, &max);

    // query statement
    if (statement == NULL && (urgent || fun)) {
      statement = stm_query(urgent, scr_action);
    }

    // get power
    al_power_state_t power = al_power_get();

    // power off (no return) if battery is low and not charging
    if (power.bat_low && !power.has_usb && !power.charging) {
      scr_power_off(true, true);
    }

    // begin draw
    gfx_begin(false, false);

    // update bar
    bar.time = lvx_fmt("%02d:%02d", hour, minute);
    if (!al_sample_valid(sample)) {
      bar.value = scr_trans()->menu__no_data;
    } else {
      if (scr_field == AL_SAMPLE_TMP) {
        bar.value = lvx_fmt(scr_temp_format(), scr_temp_convert(al_sample_read(sample, scr_field)));
      } else {
        bar.value = scr_field_str(scr_field, al_sample_read(sample, scr_field));
      }
    }
    lvx_bar_update(&bar);

    // set icon
    if (opt == 0) {
      lv_img_set_src(icon, rec_running() ? &img_file2 : &img_file1);
    } else if (opt == 1) {
      lv_img_set_src(icon, &img_folder);
    } else if (opt == 2) {
      lv_img_set_src(icon, &img_cog);
    } else if (opt == 3) {
      lv_img_set_src(icon, &img_usb);
    } else if (opt == 4) {
      lv_img_set_src(icon, &img_ble);
    } else if (opt == 5) {
      lv_img_set_src(icon, &img_engine);
    } else if (opt == 6) {
      lv_img_set_src(icon, &img_wrench);
    }

    // set fan
    if (fan_alt) {
      lv_img_set_src(fan, &img_fan2);
    } else {
      lv_img_set_src(fan, &img_fan1);
    }

    // draw chart, splitting the line at unavailable values (NaN)
    lv_canvas_fill_bg(chart, lv_color_white(), LV_OPA_COVER);
    lv_point_t points[SCR_HIST_POINTS] = {0};
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = 2;
    size_t run_start = 0, run_len = 0;
    for (size_t i = 0; i < SCR_HIST_POINTS; i++) {
      if (isnan(values[i])) {
        if (run_len > 1) {
          lv_canvas_draw_line(chart, &points[run_start], (uint32_t)run_len, &line_dsc);
        }
        run_len = 0;
        continue;
      }
      if (run_len == 0) {
        run_start = i;
      }
      points[i].x = (lv_coord_t)(i * 24 / (SCR_HIST_POINTS - 1));
      points[i].y = (lv_coord_t)al_safe_map(values[i], min, max, 14, 2);
      run_len++;
    }
    if (run_len > 1) {
      lv_canvas_draw_line(chart, &points[run_start], (uint32_t)run_len, &line_dsc);
    }

    // draw drain (empty for unavailable values)
    lv_canvas_fill_bg(drain, lv_color_white(), LV_OPA_COVER);
    lv_coord_t drain_height = 0;
    if (!isnan(values[SCR_HIST_POINTS - 1])) {
      drain_height = (lv_coord_t)al_safe_map(values[SCR_HIST_POINTS - 1], min, max, 0, 9);
    }
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();
    lv_canvas_draw_rect(drain, 1, (lv_coord_t)(1 + 9 - drain_height), 20, drain_height, &rect_dsc);

    // set bubble text
    switch (scr_lang()) {
      case SCR_DE:
        bubble.text = statement ? statement->text_de : NULL;
        break;
      case SCR_EN:
        bubble.text = statement ? statement->text_en : NULL;
        break;
      case SCR_ES:
        bubble.text = statement ? statement->text_es : NULL;
        break;
      case SCR_FR:
        bubble.text = statement ? statement->text_fr : NULL;
        break;
    }

    // update bubble
    lvx_bubble_update(&bubble);

    // update robin
    if (statement) {
      switch (statement->mood) {
        case STM_HAPPY:
          lv_img_set_src(robin, &img_robin_happy);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
          break;
        case STM_COLD:
          lv_img_set_src(robin, &img_robin_cold);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
          break;
        case STM_ANGRY1:
          lv_img_set_src(robin, &img_robin_angry1);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
          break;
        case STM_ANGRY2:
          lv_img_set_src(robin, &img_robin_angry2);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 11, -10);
          break;
        case STM_STANDING:
          lv_img_set_src(robin, &img_robin_standing);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
          break;
        case STM_POINTING:
          lv_img_set_src(robin, &img_robin_pointing);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
          break;
        case STM_WORKING:
          lv_img_set_src(robin, &img_robin_working);
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
          break;
      }
    } else {
      lv_img_set_src(robin, &img_robin_standing);
      lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10);
    }

    // end draw
    gfx_end(false, false);

    // clear flags
    urgent = false;
    fun = false;

    // await event
    sig_event_t event = sig_await(SIG_SENSOR | SIG_INTERRUPT | SIG_LAUNCH | SIG_KEYS | SIG_IDLE, 10 * 1000);

    // handle deadline (at power-test level 3 the screen is kept active to
    // allow measuring its power consumption)
    if (event.type & (SIG_SENSOR | SIG_INTERRUPT) && naos_millis() > deadline && naos_get_l("power-test") < 3) {
      event.type = SIG_TIMEOUT;
    } else if (event.type & (SIG_KEYS | SIG_LAUNCH)) {
      deadline = naos_millis() + SCR_IDLE_TIMEOUT;
    }

    // redraw on early timeouts to keep the app bar time and status current
    // also at slow sensor rates
    if (event.type == SIG_TIMEOUT && naos_millis() <= deadline) {
      continue;
    }

    // clear statement and action on any key
    if (statement != NULL && (event.type & SIG_KEYS)) {
      statement = NULL;
      scr_action = 0;
      continue;
    }

    // enter idle screen on escape
    if (event.type & (SIG_ESCAPE | SIG_IDLE)) {
      // cleanup
      gui_cleanup(false);

      // set return
      scr_return_unlock = scr_menu;

      return scr_idle;
    }

    // loop on sensor or interrupt
    if (event.type & (SIG_SENSOR | SIG_INTERRUPT)) {
      // cycle fan on sensor value
      if (event.type & SIG_SENSOR) {
        fan_alt = !fan_alt;
      }

      // show fun fact after half of deadline expired
      if (deadline - naos_millis() < SCR_IDLE_TIMEOUT / 2) {
        fun = true;
      }

      continue;
    }

    // start engine on launch
    if (event.type == SIG_LAUNCH) {
      // run engine
      scr_launch(event.plugin.file, event.plugin.mode);

      return scr_menu;
    }

    // change field on up/down, down advances like in lists
    if (event.type == SIG_DOWN) {
      scr_field_cycle(true, has_pm);
      continue;
    } else if (event.type == SIG_UP) {
      scr_field_cycle(false, has_pm);
      continue;
    }

    // change opt left/right
    if (event.type == SIG_LEFT) {
      opt--;
      if (opt < 0) {
        opt = developer ? 6 : 5;
      }
      continue;
    } else if (event.type == SIG_RIGHT) {
      opt++;
      if (opt > (developer ? 6 : 5)) {
        opt = 0;
      }
      continue;
    }

    // cleanup
    gui_cleanup(false);

    // clear action
    scr_action = 0;

    // enter idle screen on timeout
    if (event.type == SIG_TIMEOUT) {
      // set return
      scr_return_unlock = scr_menu;

      return scr_idle;
    }

    // handle enter
    if (event.type == SIG_ENTER) {
      switch (opt) {
        case 0:
          scr_file = rec_running() ? rec_file() : 0;
          return scr_view;
        case 1:
          return scr_explore;
        case 2:
          return scr_settings;
        case 3:
          return scr_usb;
        case 4:
          return scr_ble;
        case 5:
          return scr_engine;
        case 6:
          return scr_develop;
        default:
          ESP_ERROR_CHECK(ESP_FAIL);
      }
    }
  }
}

static void* scr_intro() {
  // skip if developer
  if (naos_get_b("developer")) {
    return scr_menu;
  }

  // wait a bit
  naos_delay(1000);

  // show robin
  gfx_begin(false, false);
  lv_obj_t* img = lv_img_create(lv_scr_act());
  lv_img_set_src(img, &img_robin_standing);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
  gfx_end(false, false);
  naos_delay(3000);
  gui_cleanup(false);

  // show messages
  gui_message(scr_trans()->intro__hello1, 5000);
  gui_message(scr_trans()->intro__hello2, 5000);

  for (;;) {
    // format current date/time
    uint16_t year, month, day, hour, minute, seconds;
    al_clock_get_date(&year, &month, &day);
    al_clock_get_time(&hour, &minute, &seconds);
    const char* date_time = lvx_fmt(scr_trans()->intro__watch, hour, minute, year, month, day);

    // confirm date/time
    if (gui_confirm(date_time, scr_trans()->intro__correct, scr_trans()->intro__adjust, false, SCR_ACTION_TIMEOUT)) {
      break;
    }

    // otherwise, update date/time
    if (scr_date()) {
      scr_time();
    }
  }

  // test knowledge
  if (gui_confirm(scr_trans()->intro__test, scr_trans()->yes, scr_trans()->no, false, SCR_ACTION_TIMEOUT)) {
    gui_cycle(false, scr_trans()->intro__infos, scr_trans()->next, scr_trans()->back);
  }

  // show end
  gui_message(scr_trans()->intro__end, 5000);

  // section action
  scr_action = STM_FROM_INTRO;

  return scr_menu;
}

/* Management */

static void* (*scr_handler)();

static void scr_task() {
  // call handlers
  for (;;) {
    void* next = scr_handler();
    scr_handler = next;
  }
}

static void scr_screen_index_set(int32_t value) {
  scr_screen_index = value;
  scr_screen_start = naos_millis();
}

static naos_param_t scr_params[] = {
    {.name = "idle-scr-index",
     .type = NAOS_LONG,
     .func_l = scr_screen_index_set,
     .mode = NAOS_VOLATILE,
     .skip_func_init = true},
    {.name = "idle-auto-cycle", .type = NAOS_BOOL, .sync_b = &scr_auto_cycle, .default_b = true},
};

void scr_run(al_trigger_t trigger) {
  // register parameters
  for (int i = 0; i < NAOS_COUNT(scr_params); i++) {
    naos_register(&scr_params[i]);
  }

  // handle return
  scr_handler = scr_menu;
  if (trigger == AL_RESET) {
    scr_handler = scr_intro;
  } else if (trigger == AL_BUTTON && scr_return_unlock != NULL) {
    if (scr_return_unlock_mask != 0 && (al_buttons_wakeup() & scr_return_unlock_mask) != 0) {
      scr_handler = scr_return_unlock;
      scr_return_unlock = NULL;
      scr_return_unlock_mask = 0;
    } else {
      scr_handler = scr_return_timeout;
      scr_return_timeout = NULL;
    }
  } else if ((trigger == AL_TIMEOUT || trigger == AL_INTERRUPT) && scr_return_timeout != NULL) {
    scr_handler = scr_return_timeout;
    scr_return_timeout = NULL;
  }

  // determine wake state: a reset or a button press means a user is present,
  // while timer and interrupt wake ups only perform background work before
  // returning to sleep
  if (trigger == AL_RESET || trigger == AL_BUTTON) {
    scr_wake_up("trigger");
  } else {
    naos_log("scr: dozing");
  }

  // run screen task
  naos_run("scr", 8192, 1, scr_task);
}
