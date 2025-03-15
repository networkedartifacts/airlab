#include <naos.h>
#include <naos/sys.h>
#include <naos/cpu.h>
#include <art32/numbers.h>
#include <lvgl.h>
#include <math.h>
#include <utime.h>
#include <time.h>

#include <al/core.h>
#include <al/accel.h>
#include <al/led.h>
#include <al/power.h>
#include <al/clock.h>
#include <al/sensor.h>

#include "gfx.h"
#include "sig.h"
#include "fnt.h"
#include "img.h"
#include "lvx.h"
#include "sys.h"
#include "rec.h"
#include "dev.h"
#include "stm.h"

#define SCR_ACTION_TIMEOUT 10000
#define SCR_IDLE_TIMEOUT 30000
#define SCR_CHART_POINTS 72
#define SCR_MIN_RESOLUTION 5000

typedef enum {
  SCR_LED_USB = 1 << 0,
  SCR_LED_OFF = 1 << 1,
} scr_led_flag_t;

static stm_action_t scr_action = 0;
DEV_KEEP static size_t scr_file = 0;
DEV_KEEP static int64_t scr_saver_enter = 0;
DEV_KEEP static void* scr_return_timeout = NULL;
DEV_KEEP static void* scr_return_unlock = NULL;
static scr_led_flag_t scr_led_flags = 0;

/* Helpers */

static const char* scr_fmt(const char* fmt, ...) {
  // prepare global storage
  static char buffers[8][64];
  static uint8_t num = 0;

  // select string
  char* str = buffers[num];
  if (++num >= 8) {
    num = 0;
  }

  // format string
  va_list args;
  va_start(args, fmt);
  vsnprintf(str, 64, fmt, args);
  va_end(args);

  return str;
}

static const char* scr_ms2str(int32_t ms) {
  if (ms > 1000 * 60 * 60) {  // hours
    return scr_fmt("%dh", ms / 1000 / 60 / 60);
  } else if (ms > 1000 * 60) {  // minutes
    return scr_fmt("%dm", ms / 1000 / 60);
  } else {  // seconds
    return scr_fmt("%ds", ms / 1000);
  }
}

static void scr_cleanup(bool refresh) {
  // clear group and screen
  gfx_begin(refresh, false);
  lv_disp_set_rotation(NULL, LV_DISP_ROT_NONE);
  lv_group_remove_all_objs(gfx_get_group());
  lv_obj_clean(lv_scr_act());
  gfx_end(false);

  // await refresh
  if (refresh) {
    sig_await(SIG_REFRESH, 0);
  }
}

static void scr_write(const char* text) {
  // show message
  gfx_begin(false, false);
  lv_obj_t* lbl = lv_label_create(lv_scr_act());
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_line_space(lbl, 6, LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  gfx_end(false);
}

static void scr_message(const char* text, uint32_t timeout) {
  // show message
  scr_write(text);

  // wait some time
  sig_await(SIG_KEYS | SIG_TIMEOUT, timeout);

  // cleanup
  scr_cleanup(false);
}

static int scr_list(const char** labels, const char* select, const char* cancel, int start) {
  // prepare variables
  static int selected = 0;
  static int offset = 0;

  // count labels
  int total = 0;
  while (labels[total] != NULL) {
    total++;
  }

  // handle empty
  if (total <= 0) {
    return -1;
  }

  // set selected
  selected = start;

  // adjust offset
  if (selected > offset + 3) {
    offset = selected - 3;
  } else if (selected < offset) {
    offset = selected;
  }

  // begin draw
  gfx_begin(false, false);

  // add list
  lv_obj_t* rects[4];
  lv_obj_t* names[4];
  for (int i = 0; i < 4; i++) {
    rects[i] = lv_obj_create(lv_scr_act());
    names[i] = lv_label_create(lv_scr_act());
    lv_obj_set_size(rects[i], lv_pct(100), 25);
    lv_obj_align(rects[i], LV_ALIGN_TOP_LEFT, 0, 0 + i * 25);
    lv_obj_align(names[i], LV_ALIGN_TOP_LEFT, 5, 5 + i * 25);
    lv_obj_set_style_border_width(rects[i], 0, LV_PART_MAIN);
    lv_obj_set_style_radius(rects[i], 0, LV_PART_MAIN);
  }

  // add signs
  lvx_sign_t back = {
      .title = "B",
      .text = cancel,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_t open = {
      .title = "A",
      .text = select,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_create(&open, lv_scr_act());

  // add info
  lv_obj_t* info = lv_label_create(lv_scr_act());
  lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -5);

  // end draw
  gfx_end(true);

  for (;;) {
    // begin draw
    gfx_begin(false, false);

    // fill list
    for (int i = 0; i < +4; i++) {
      // get index
      int index = offset + i;

      // handle empty
      if (index >= total) {
        // clear labels and rectangle
        lv_label_set_text(names[i], "");
        lv_obj_set_style_bg_color(rects[i], lv_color_white(), LV_PART_MAIN);

        continue;
      }

      // update labels
      lv_label_set_text(names[i], labels[index]);

      // handle selected
      if (index == selected) {
        lv_obj_set_style_text_color(names[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(rects[i], lv_color_black(), LV_PART_MAIN);
      } else {
        lv_obj_set_style_text_color(names[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(rects[i], lv_color_white(), LV_PART_MAIN);
      }
    }

    // update info
    lv_label_set_text(info, scr_fmt("%d/%d", selected + 1, total));

    // end draw
    gfx_end(false);

    // await event
    sig_event_t event = sig_await(SIG_UP | SIG_DOWN | SIG_META | SIG_SCROLL, SCR_ACTION_TIMEOUT);

    // handle arrows
    if ((event.type & (SIG_UP | SIG_DOWN | SIG_SCROLL)) != 0) {
      if (event.type == SIG_SCROLL) {
        selected += (int)(event.touch * 2);
      } else {
        selected += event.type == SIG_UP ? -1 : 1;
      }
      while (selected < 0) {
        selected += total;
      }
      while (selected > total - 1) {
        selected -= total;
      }
      if (selected > offset + 3) {
        offset = selected - 3;
      } else if (selected < offset) {
        offset = selected;
      }
      continue;
    }

    /* handle meta and timeout */

    // cleanup
    scr_cleanup(false);

    // handle escape and timeout
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return -1;
    }

    /* handle enter */

    return selected;
  }
}

static void scr_power_off() {
  // turn off LED
  scr_led_flags |= SCR_LED_OFF;

  // cleanup screen
  scr_cleanup(true);

  // clear returns
  scr_return_timeout = NULL;
  scr_return_unlock = NULL;

  // power off
  al_power_off();
}

/* Translations */

typedef enum {
  SCR_DE,
  SCR_EN,
} scr_lang_t;

// TODO: Make settings persistent.
static scr_lang_t scr_lang = SCR_EN;

typedef struct {
  const char* back;
  const char* next;
  const char* cancel;
  const char* measurement;
  const char* exit__stop;
  const char* exit__back;
  const char* exit__stopped;
  const char* create__full;
  const char* create__new;
  const char* create__name;
  const char* create__length;
  const char* create__start;
  const char* delete__confirm;
  const char* delete__delete;
  const char* delete__deleted;
  const char* edit__analyse;
  const char* edit__delete;
  const char* explore__empty;
  const char* explore__open;
  const char* usb__running;
  const char* usb__disconnected;
  const char* usb__active;
  const char* usb__eject;
  const char* reset__confirm;
  const char* reset__yes;
  const char* reset__no;
  const char* reset__reset;
  const char* settings__title;
  const char* settings__storage;
  const char* settings__date_time;
  const char* settings__language;
  const char* settings__off;
  const char* settings__reset;
  const char* menu__no_data;
  const char* time__message;
  const char* time__continue;
  const char* date__message;
  const char* language__message;
  const char* intro_message;
} scr_trans_t;

static const scr_trans_t scr_trans_map[] = {
    [SCR_DE] =
        {
            .back = "Zurück",
            .next = "Weiter",
            .cancel = "Abbrechen",
            .measurement = "Messung %u",
            .exit__stop = "%s beenden",
            .exit__back = "Zurück zum Labor",
            .exit__stopped = "%s\n beendet!",
            .create__full = "Speicher voll!",
            .create__new = "Neue Messung erstellen?",
            .create__name = "Messung %u",
            .create__length = "Länge ca. %d-%d Stunden",
            .create__start = "Starten",
            .delete__confirm = "%s\nwirklich löschen?",
            .delete__delete = "Löschen",
            .delete__deleted = "Messung %d\nerfolgreich gelöscht!",
            .edit__analyse = "Analysieren",
            .edit__delete = "Löschen",
            .explore__empty = "Keine gespeicherte\nMessungen...",
            .explore__open = "Öffnen",
            .usb__running = "Messung läuft!",
            .usb__disconnected = "USB nicht angeschlossen!",
            .usb__active = "USB-Modus Aktiv",
            .usb__eject = "USB-Verbindung getrennt",
            .reset__confirm = "Air Lab\nwirklich zurücksetzen?",
            .reset__yes = "Ja",
            .reset__no = "Nein",
            .reset__reset = "Air Lab\nerfolgreich zurückgesetzt!",
            .settings__title = "Einstellungen",
            .settings__storage = "Speicher: %.1f%% belegt",
            .settings__date_time = "Datum & Zeit",
            .settings__language = "Sprache",
            .settings__off = "Ausschalten",
            .settings__reset = "Zurücksetzen",
            .menu__no_data = "Keine Daten",
            .time__message = "Und wie spät ist es gerade?",
            .time__continue = "Wie die Zeit vergeht...\nKomm, lass uns ins Labor gehen.",
            .date__message = "Ich habe zieeemlich\nlang geschlafen!\nWelcher Tag ist heute?",
            .language__message = "In welcher Sprache\nmöchtest du quatschen?",
            .intro_message = "Hi! Ich bin Robin,\nProfessor für Luftwiss-\nenschaften im Air Lab.",
        },
    [SCR_EN] =
        {
            .back = "Back",
            .next = "Next",
            .cancel = "Cancel",
            .measurement = "Measurement %u",
            .exit__stop = "Stop %s",
            .exit__back = "Back to Lab",
            .exit__stopped = "%s\n stopped!",
            .create__full = "Storage full!",
            .create__new = "Create new measurement?",
            .create__name = "Measurement %u",
            .create__length = "Length approx. %d-%d hours",
            .create__start = "Start",
            .delete__confirm = "Really delete %s?",
            .delete__delete = "Delete",
            .delete__deleted = "Measurement %d\nsuccessfully deleted!",
            .edit__analyse = "Analyse",
            .edit__delete = "Delete",
            .explore__empty = "No saved\nmeasurements...",
            .explore__open = "Open",
            .usb__running = "Measurement running!",
            .usb__disconnected = "USB not connected!",
            .usb__active = "USB Mode Active",
            .usb__eject = "USB-Connection disconnected",
            .reset__confirm = "Fully Reset Air Lab?",
            .reset__yes = "Yes",
            .reset__no = "No",
            .reset__reset = "Air Lab\nsuccessfully reset!",
            .settings__title = "Settings",
            .settings__storage = "Storage: %.1f%% used",
            .settings__date_time = "Date & Time",
            .settings__language = "Language",
            .settings__off = "Power Off",
            .settings__reset = "Reset",
            .menu__no_data = "No Data",
            .time__message = "What time is it?",
            .time__continue = "Time flies...\nLet's go to the lab.",
            .date__message = "I slept for a\nloooong time!\nWhat day is it?",
            .language__message = "In which language\nwould you like to chat?",
            .intro_message = "Hi! I'm Robin,\nprofessor of air\nsciences at Air Lab.",
        },
};

static const scr_trans_t* scr_trans() { return &scr_trans_map[scr_lang]; }

/* Formatters */

static const char* scr_file_name(dat_file_t* file) { return scr_fmt(scr_trans()->measurement, file->head.num); }

static const char* scr_file_date(dat_file_t* file) {
  static char buf[24];

  // format date
  time_t time = (time_t)(file->head.start / 1000);
  struct tm ts = *gmtime(&time);
  strftime(buf, sizeof(buf), "%d.%m.%Y", &ts);
  return buf;
}

/* Screens */

static void* scr_info();
static void* scr_saver();
static void* scr_view();
static void* scr_edit();
static void* scr_explore();
static void* scr_menu();
static void* scr_settings();
static void* scr_develop();
static void* scr_date();
static void* scr_language();
static void* scr_intro();

static void* scr_test_bubbles() {
  // begin draw
  gfx_begin(false, false);

  // add bubble
  lvx_bubble_t bubble = {};
  lvx_bubble_create(&bubble, lv_scr_act());

  // add signs
  lvx_sign_t back = {.title = "B", .text = scr_trans()->cancel, .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_t next = {.title = ">", .text = scr_trans()->next, .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&next, lv_scr_act());

  // end draw
  gfx_end(true);

  // prepare index
  int index = 0;

  for (;;) {
    // begin draw
    gfx_begin(false, false);

    // set bubble text
    switch (scr_lang) {
      case SCR_DE:
        bubble.text = stm_get(index)->text_de;
        break;
      case SCR_EN:
        bubble.text = stm_get(index)->text_en;
        break;
    }

    // update bubble
    lvx_bubble_update(&bubble);

    // end draw
    gfx_end(false);

    // await event
    sig_event_t event = sig_await(SIG_ESCAPE | SIG_RIGHT, 0);

    // handle right
    if (event.type == SIG_RIGHT) {
      index++;
      if (!stm_get(index)) {
        index = 0;
      }
      continue;
    }

    /* handle escape */

    // cleanup screen
    scr_cleanup(false);

    return scr_develop;
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
  gfx_end(true);

  for (;;) {
    // get power
    al_power_state_t bat = al_power_get();

    // get date and time
    uint16_t year, month, day, hour, minute, seconds;
    sys_get_date(&year, &month, &day);
    sys_get_time(&hour, &minute, &seconds);

    // get CPU usage
    float cpu0 = 0, cpu1 = 0;
    naos_cpu_get(&cpu0, &cpu1);

    // prepare text
    const char* text = scr_fmt("%llds - %.0f%% - P%d - F%d\n%04d-%02d-%02d %02d:%02d:%02d\n%lu kB - %.1f%% - %.1f%%",
                               naos_millis() / 1000, bat.battery * 100, bat.usb, bat.fast, year, month, day, hour,
                               minute, seconds, esp_get_free_heap_size() / 1024, cpu0 * 100, cpu1 * 100);

    // update label
    gfx_begin(false, false);
    lv_label_set_text(label, text);
    gfx_end(false);

    // await event
    sig_event_t event = sig_await(SIG_KEYS, 1000);

    // loop on timeout
    if (event.type == SIG_TIMEOUT) {
      continue;
    }

    // cleanup
    scr_cleanup(false);

    return scr_develop;
  }
}

static void* scr_saver() {
  // prepare variables
  DEV_KEEP static bool right = true;

  // set timeout return
  scr_return_timeout = scr_saver;

  // begin draw
  gfx_begin(false, false);

  // add icons
  lv_obj_t* battery = lv_img_create(lv_scr_act());
  lv_obj_t* record = NULL;
  if (rec_running()) {
    record = lv_img_create(lv_scr_act());
    lv_img_set_src(record, &img_record);
  }

  // add values
  lv_obj_t* time = lv_label_create(lv_scr_act());
  lv_obj_t* co2 = lv_label_create(lv_scr_act());
  lv_obj_t* tmp = lv_label_create(lv_scr_act());
  lv_obj_t* hum = lv_label_create(lv_scr_act());

  // add big values
  lv_obj_t* co2_big = lv_label_create(lv_scr_act());
  lv_obj_t* tmp_big = lv_label_create(lv_scr_act());
  lv_obj_t* hum_big = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(co2_big, &fnt_24, LV_PART_MAIN);
  lv_obj_set_style_text_font(tmp_big, &fnt_24, LV_PART_MAIN);
  lv_obj_set_style_text_font(hum_big, &fnt_24, LV_PART_MAIN);

  // end draw
  gfx_end(true);

  for (;;) {
    // get time
    uint16_t hour, minute, seconds;
    sys_get_time(&hour, &minute, &seconds);

    // read sensor
    al_sensor_state_t sensor = al_sensor_get();

    // await sensor if missing (deep sleep return)
    if (!sensor.ok) {
      sig_await(SIG_SENSOR, 0);
      sensor = al_sensor_get();
    }

    // read power
    al_power_state_t power = al_power_get();

    // get accelerometer state
    al_accel_state_t acc = al_accel_get();

    // flip side
    right = !right;

    // begin draw
    gfx_begin(false, false);

    // determine vertical
    bool vertical = !acc.lock && (acc.rot == 90 || acc.rot == 270);

    // set display rotation
    if (!acc.lock) {
      lv_disp_set_rotation(NULL, acc.rot / 90);
    } else {
      lv_disp_set_rotation(NULL, LV_DISP_ROT_NONE);
    }

    // update battery
    if (power.usb && power.charging) {
      lv_img_set_src(battery, &img_power);
    } else if (power.battery > 0.75) {
      lv_img_set_src(battery, &img_bat3);
    } else if (power.battery > 0.5) {
      lv_img_set_src(battery, &img_bat2);
    } else if (power.battery > 0.25) {
      lv_img_set_src(battery, &img_bat1);
    } else {
      lv_img_set_src(battery, &img_bat0);
    }

    // TODO: Show VOC and NOx values.

    // update values
    lv_label_set_text(time, scr_fmt("%02d:%02d", hour, minute));
    if (vertical) {
      lv_label_set_text(co2, "ppm CO2");
      lv_label_set_text(tmp, "° Celsius");
      lv_label_set_text(hum, "% RH");
      lv_label_set_text(co2_big, scr_fmt("%.0f", sensor.co2));
      lv_label_set_text(tmp_big, scr_fmt("%.1f", sensor.tmp));
      lv_label_set_text(hum_big, scr_fmt("%.1f", sensor.hum));
    } else {
      lv_label_set_text(co2, scr_fmt("%.0f ppm", sensor.co2));
      lv_label_set_text(tmp, scr_fmt("%.1f °C", sensor.tmp));
      lv_label_set_text(hum, scr_fmt("%.1f%% RH", sensor.hum));
    }

    // align objects
    if (vertical) {
      lv_obj_align(co2_big, LV_ALIGN_TOP_MID, 0, 25);
      lv_obj_align(co2, LV_ALIGN_TOP_MID, 0, 25 + 27);
      lv_obj_align(tmp_big, LV_ALIGN_TOP_MID, 0, 100);
      lv_obj_align(tmp, LV_ALIGN_TOP_MID, 0, 100 + 27);
      lv_obj_align(hum_big, LV_ALIGN_TOP_MID, 0, 175);
      lv_obj_align(hum, LV_ALIGN_TOP_MID, 0, 175 + 27);
      lv_obj_align(time, LV_ALIGN_BOTTOM_RIGHT, -25, -25);
      lv_obj_align(battery, LV_ALIGN_BOTTOM_LEFT, 25, -25);
      if (record != NULL) {
        lv_obj_align(record, LV_ALIGN_BOTTOM_LEFT, 45, -25);
      }
    } else {
      lv_align_t align = right ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT;
      lv_obj_align(battery, align, right ? -20 : 20, 19);
      if (record != NULL) {
        lv_obj_align(record, align, right ? -36 : 36, 20);
      }
      lv_obj_align(time, align, right ? -19 : 19, 41);
      lv_obj_align(co2, align, right ? -19 : 19, 59);
      lv_obj_align(tmp, align, right ? -19 : 19, 77);
      lv_obj_align(hum, align, right ? -19 : 19, 95);
      lv_obj_align(co2_big, 0, -100, -100);
      lv_obj_align(tmp_big, 0, -100, -100);
      lv_obj_align(hum_big, 0, -100, -100);
    }

    // end draw
    gfx_end(false);

    // await draw
    naos_delay(1000);

    /* Sleep Control */

    // determine duration
    int64_t duration = sys_get_timestamp() - scr_saver_enter;

    // power off if battery is low and not charging
    if (power.battery < 0.10 && !power.usb && !power.charging) {
      scr_power_off();
    }

    // deep sleep for 1min if not recording
    if (!rec_running()) {
      // perform deep sleep
      al_sleep(true, 60 * 1000);

      // no return
    }

    // otherwise, light sleep for 5s-30s (0-5min) if recording
    int64_t timeout = a32_safe_map_l(duration, 0, 300000, 5000, 30000);
    al_sleep(false, timeout);

    // capture enter when unlocked
    al_trigger_t trigger = al_trigger();
    if (trigger == AL_BUTTON) {
      sig_await(SIG_ENTER, 1000);
    }

    // handle unlock
    if (trigger == AL_BUTTON) {
      break;
    }

    // await next measurement or stop
    sig_await(SIG_APPEND | SIG_STOP, 0);
  }

  // cleanup
  scr_cleanup(false);

  return scr_return_unlock;
}

static void* scr_exit() {
  // get file
  dat_file_t* file = dat_get_file(scr_file);

  // begin draw
  gfx_begin(false, true);

  // add signs
  lvx_sign_t stop = {
      .title = "A",
      .text = scr_fmt(scr_trans()->exit__stop, scr_file_name(file)),
      .align = LV_ALIGN_CENTER,
      .offset = -15,
  };
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->exit__back,
      .align = LV_ALIGN_CENTER,
      .offset = 15,
  };
  lvx_sign_create(&stop, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false);

  // await event
  sig_event_t event = sig_await(SIG_META, SCR_ACTION_TIMEOUT);

  // cleanup
  scr_cleanup(false);

  // go back to view on timeout
  if (event.type == SIG_TIMEOUT) {
    return scr_view;
  }

  // set action
  if (scr_action == 0) {
    scr_action = STM_FROM_MEASUREMENT;
  }

  // handle enter
  if (event.type == SIG_ENTER) {
    // get file
    dat_file_t* file = dat_get_file(rec_file());

    // stop recording
    rec_stop();

    // show message
    scr_message(scr_fmt(scr_trans()->exit__stopped, scr_file_name(file)), 2000);

    // set action
    scr_action = STM_COMP_MEASUREMENT;
  }

  return scr_menu;
}

static void* scr_view() {
  // prepare variables
  static int8_t mode = 0;  // co2, tmp, hum
  static bool advanced = false;
  static dat_point_t scr_points[SCR_CHART_POINTS];

  // zero points
  memset(scr_points, 0, sizeof(scr_points));

  // get file
  dat_file_t* file = dat_get_file(scr_file);

  // check recording
  bool recording = rec_running() && rec_file() == scr_file;

  // prepare position
  int32_t position = 0;
  if (!recording) {
    position = file->stop / 2;
  }

  // begin draw
  gfx_begin(false, false);

  // add bar
  lvx_bar_t bar = {0};
  lvx_bar_create(&bar, lv_scr_act());

  // add chart
  lv_obj_t* chart = lv_canvas_create(lv_scr_act());
  static lv_color_t chart_buffer[LV_CANVAS_BUF_SIZE_TRUE_COLOR(288, 96)] = {0};
  lv_canvas_set_buffer(chart, chart_buffer, 288, 96, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(chart, LV_ALIGN_BOTTOM_LEFT, 5, -5);
  lv_canvas_fill_bg(chart, lv_color_white(), LV_OPA_COVER);

  // end draw
  gfx_end(true);

  // prepare deadline
  int64_t deadline = naos_millis() + SCR_IDLE_TIMEOUT;

  for (;;) {
    // update recording
    recording = rec_running() && rec_file() == scr_file;

    // adjust position if recording
    if (recording) {
      position = file->stop;
    }

    // calculate resolution
    int32_t resolution = file->stop / SCR_CHART_POINTS;
    if (recording) {
      resolution = SCR_MIN_RESOLUTION;
    } else if (advanced) {
      resolution = file->stop / 10 / SCR_CHART_POINTS;
    }
    if (resolution < SCR_MIN_RESOLUTION) {
      resolution = SCR_MIN_RESOLUTION;
    }

    // calculate range
    int32_t start = 0;
    int32_t end = SCR_CHART_POINTS * resolution;
    if (recording) {
      start = position - SCR_CHART_POINTS / 3 * 2 * resolution;
      end = position + SCR_CHART_POINTS / 3 * resolution;
      if (start < 0) {
        end += start * -1;
        start = 0;
      }
    } else if (advanced) {
      start = position - SCR_CHART_POINTS / 2 * resolution;
      end = position + SCR_CHART_POINTS / 2 * resolution;
      if (start < 0) {
        end += start * -1;
        start = 0;
      }
      if (end > file->stop) {
        int32_t shift = fminf(start, end - file->stop);
        end -= shift;
        start -= shift;
      }
    }

    // calculate index
    size_t index = roundf(a32_safe_map_f(position, start, end, 0, SCR_CHART_POINTS - 1));

    // query points
    if (file->size > 0) {
      size_t num = dat_query(file->head.num, scr_points, SCR_CHART_POINTS, start, resolution);
      if (recording) {
        index = num - 1;
      }
    }

    // find marks
    uint8_t marks[SCR_CHART_POINTS] = {0};
    for (uint8_t i = 0; i < DAT_MARKS; i++) {
      if (file->head.marks[i] > 0) {
        int32_t mark = roundf(a32_map_f(file->head.marks[i], start, end, 0, SCR_CHART_POINTS - 1));
        if (mark >= 0 && mark <= SCR_CHART_POINTS - 1) {
          marks[(size_t)mark] = i + 1;
        }
      }
    }

    // select current point
    dat_point_t current = scr_points[index];

    // parse time
    uint16_t hour;
    uint16_t minute;
    sys_conv_timestamp(file->head.start + (int64_t)current.offset, &hour, &minute, NULL);

    // begin draw
    gfx_begin(false, advanced);

    // update bar
    bar.time = scr_fmt("%02d:%02d", hour, minute);
    if (recording) {
      bar.mark = file->marks > 0 ? scr_fmt("(M%d)", file->marks) : "";
    } else {
      bar.mark = marks[index] > 0 ? scr_fmt("(M%d)", marks[index]) : "";
    }
    if (mode == 0) {
      bar.value = scr_fmt("%.0f ppm CO2", current.co2);
    } else if (mode == 1) {
      bar.value = scr_fmt("%.1f °C", current.tmp);
    } else if (mode == 2) {
      bar.value = scr_fmt("%.1f%% RH", current.hum);
    } else if (mode == 3) {
      bar.value = scr_fmt("%.0f VOC", current.voc);
    } else if (mode == 4) {
      bar.value = scr_fmt("%.0f NOx", current.nox);
    }
    lvx_bar_update(&bar);

    // TODO: Adjust range if exceeding maximum.

    // draw chart bars and marks
    lv_canvas_fill_bg(chart, lv_color_white(), LV_OPA_COVER);
    float range = mode == 0 ? 3000 : mode > 2 ? 500 : 100;
    lv_draw_line_dsc_t bar_desc;
    lv_draw_line_dsc_init(&bar_desc);
    bar_desc.width = 2;
    for (size_t i = 0; i < SCR_CHART_POINTS; i++) {
      float value = mode == 0   ? scr_points[i].co2
                    : mode == 1 ? scr_points[i].tmp
                    : mode == 2 ? scr_points[i].hum
                    : mode == 3 ? scr_points[i].voc
                                : scr_points[i].nox;
      lv_coord_t h = 2 + a32_safe_map_f(value, 0, range, 0, 78);
      lv_point_t points[2] = {{.x = 1 + i * 4, .y = 80}, {.x = 1 + i * 4, .y = 80 - h}};
      lv_canvas_draw_line(chart, points, 2, &bar_desc);
      if (marks[i] > 0) {
        points[0].y = 82;
        points[1].y = 84;
        lv_canvas_draw_line(chart, points, 2, &bar_desc);
      }
    }

    // draw chart arrows
    if (advanced) {
      lv_draw_img_dsc_t img_draw;
      lv_draw_img_dsc_init(&img_draw);
      if (start > 0) {
        lv_canvas_draw_img(chart, 0, 96 - 7, &img_arrow_left, &img_draw);
      }
      if (end < file->stop) {
        lv_canvas_draw_img(chart, 288 - 9, 96 - 7, &img_arrow_right, &img_draw);
      }
    }

    // draw chart labels
    lv_draw_label_dsc_t lbl_desc;
    lv_draw_label_dsc_init(&lbl_desc);
    lbl_desc.font = &fnt_8;
    lbl_desc.align = LV_TEXT_ALIGN_LEFT;
    for (size_t i = 0; i < 3; i++) {
      // labels are position on the nearest minute mark using the following grid
      // < 1/6 |   1/3   |   1/3   |   1/3   | 1/6 >

      // get minuted aligned position
      float step = (float)(end - start) / 6.f;
      float pos = (float)start + step + (float)(i) * (step * 2);
      pos = roundf(pos / 60000) * 60000;

      // format label
      sys_conv_timestamp(file->head.start + (int64_t)(pos), &hour, &minute, NULL);
      const char* str = scr_fmt("%02d:%02d", hour, minute);

      // calculate coordinate
      lv_coord_t x = (lv_coord_t)a32_map_f(pos, (float)start, (float)end, 0, 288);
      x -= lv_txt_get_width(str, strlen(str), &fnt_8, 0, 0) / 2;

      // draw label
      lv_canvas_draw_text(chart, x, 88, 99, &lbl_desc, str);
    }

    // draw chart position if not recording
    if (!recording) {
      lv_point_t points[2] = {{.x = 1 + index * 4, .y = 87}, {.x = 1 + index * 4, .y = 96}};
      lv_canvas_draw_line(chart, points, 2, &bar_desc);
    }

    // end draw
    gfx_end(false);

    // await event
    sig_type_t filter = SIG_KEYS | SIG_SCROLL;
    if (recording) {
      filter |= SIG_APPEND | SIG_STOP;
    }
    sig_event_t event = sig_await(filter, SCR_IDLE_TIMEOUT);

    // handle deadline
    if (event.type == SIG_APPEND && naos_millis() > deadline) {
      event.type = SIG_TIMEOUT;
    } else if ((event.type & (SIG_KEYS | SIG_SCROLL)) != 0) {
      deadline = naos_millis() + SCR_IDLE_TIMEOUT;
    }

    // update on append or stop
    if (event.type == SIG_APPEND || event.type == SIG_STOP) {
      continue;
    }

    // handle idle timeout
    if (event.type == SIG_TIMEOUT) {
      // cleanup
      scr_cleanup(false);

      // set return
      scr_return_unlock = scr_view;

      // set enter
      scr_saver_enter = sys_get_timestamp();

      return scr_saver;
    }

    // handle escape
    if (event.type == SIG_ESCAPE) {
      // handle advanced
      if (advanced) {
        advanced = false;
        continue;
      }

      // cleanup
      scr_cleanup(false);

      // handle recording
      if (recording) {
        return scr_exit;
      }

      // set action
      scr_action = STM_FROM_ANALYSIS;

      return scr_edit;
    }

    // add mark on enter
    if (event.type == SIG_ENTER) {
      if (recording) {
        rec_mark();
      } else {
        advanced = true;
      }
      continue;
    }

    // change mode on up/down
    if (event.type == SIG_UP) {
      mode++;
      if (mode > 4) {
        mode = 0;
      }
      continue;
    } else if (event.type == SIG_DOWN) {
      mode--;
      if (mode < 0) {
        mode = 4;
      }
      continue;
    }

    // change position on left/right/scroll if not recording
    if (!recording) {
      if (event.type == SIG_LEFT) {
        position -= resolution * (event.repeat ? 5 : 1);
      } else if (event.type == SIG_RIGHT) {
        position += resolution * (event.repeat ? 5 : 1);
      } else if (event.type == SIG_SCROLL) {
        position += resolution * (int32_t)(event.touch * 2);
      }
      if (position > file->stop) {
        position = file->stop;
      }
      if (position < 0) {
        position = 0;
      }
    }
  }
}

static void* scr_create() {
  // get free points
  uint32_t points = rec_free(true);

  // handle no space
  if (!points) {
    scr_message(scr_trans()->create__full, 2000);
    return scr_menu;
  }

  // calculate min and max time
  uint32_t min_hours = points / 12 / 60;  // 12 points per minute
  uint32_t max_hours = points / 2 / 60;   // 2 points per minute

  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, scr_trans()->create__new);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);

  // add name
  lv_obj_t* name = lv_label_create(lv_scr_act());
  lv_label_set_text(name, scr_fmt(scr_trans()->create__name, dat_next()));
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 5, 26);

  // add mode
  lv_obj_t* mode = lv_label_create(lv_scr_act());
  lv_label_set_text(mode, "CO2, TEMP, RH");
  lv_obj_align(mode, LV_ALIGN_TOP_LEFT, 5, 47);

  // add length
  lv_obj_t* length = lv_label_create(lv_scr_act());
  lv_label_set_text(length, scr_fmt(scr_trans()->create__length, min_hours, max_hours));
  lv_obj_align(length, LV_ALIGN_TOP_LEFT, 5, 68);

  // add signs
  lvx_sign_t start = {
      .title = "A",
      .text = scr_trans()->create__start,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&start, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false);

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_META, SCR_ACTION_TIMEOUT);

    // cleanup
    scr_cleanup(false);

    // handle escape and timeout
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return scr_menu;
    }

    /* handle enter */

    // create measurement
    scr_file = dat_create(sys_get_timestamp());

    // get file
    dat_file_t* file = dat_get_file(scr_file);

    // start recording
    rec_start(scr_file);

    // set action
    if (file->head.num == 1) {
      scr_action = STM_START_FIRST_MEASUREMENT;
    } else {
      scr_action = STM_START_MEASUREMENT;
    }

    return scr_view;
  }
}

static void* scr_delete() {
  // begin draw
  gfx_begin(false, false);

  // get file
  dat_file_t* file = dat_get_file(scr_file);

  // add text
  lv_obj_t* text = lv_label_create(lv_scr_act());
  lv_label_set_text(text, scr_fmt(scr_trans()->delete__confirm, scr_file_name(file)));
  lv_obj_align(text, LV_ALIGN_TOP_MID, 0, 25);
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  // add signs
  lvx_sign_t next = {
      .title = "A",
      .text = scr_trans()->delete__delete,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&next, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false);

  // await event
  sig_event_t event = sig_await(SIG_META, SCR_ACTION_TIMEOUT);

  // cleanup
  scr_cleanup(false);

  // handle escape and timeout
  if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
    return scr_edit;
  }

  /* handle enter */

  // capture num
  uint16_t num = file->head.num;

  // delete file
  dat_delete(file->head.num);

  // show message
  scr_message(scr_fmt(scr_trans()->delete__deleted, num), 2000);

  return scr_explore;
}

static void* scr_edit() {
  // begin draw
  gfx_begin(false, false);

  // get file
  dat_file_t* file = dat_get_file(scr_file);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, scr_file_name(file));
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);

  // add date
  lv_obj_t* date = lv_label_create(lv_scr_act());
  lv_label_set_text(date, scr_file_date(file));
  lv_obj_align(date, LV_ALIGN_TOP_LEFT, 5, 26);

  // add length
  lv_obj_t* length = lv_label_create(lv_scr_act());
  lv_label_set_text(length, scr_ms2str(file->stop));
  lv_obj_align(length, LV_ALIGN_TOP_MID, 0, 26);

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
  lvx_sign_create(&analyze, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&delete, lv_scr_act());

  // end draw
  gfx_end(false);

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_META | SIG_LEFT, SCR_ACTION_TIMEOUT);

    // cleanup
    scr_cleanup(false);

    // handle event
    switch (event.type) {
      case SIG_ESCAPE:
      case SIG_TIMEOUT:
        return scr_explore;
      case SIG_LEFT:
        return scr_delete;
      case SIG_ENTER:
        return scr_view;
      default:
        ESP_ERROR_CHECK(ESP_FAIL);
    }
  }
}

static void* scr_explore() {
  // prepare variables
  static int selected;
  static int offset;

  // clear variables
  selected = 0;
  offset = 0;

  // get total length
  size_t total = dat_num_files();

  // ignore last if recording
  if (rec_running()) {
    total--;
  }

  // handle empty
  if (total == 0) {
    // show message
    scr_message(scr_trans()->explore__empty, 2000);

    return scr_menu;
  }

  // begin draw
  gfx_begin(false, false);

  // add list
  lv_obj_t* rects[4];
  lv_obj_t* names[4];
  lv_obj_t* dates[4];
  for (int i = 0; i < 4; i++) {
    rects[i] = lv_obj_create(lv_scr_act());
    names[i] = lv_label_create(lv_scr_act());
    dates[i] = lv_label_create(lv_scr_act());
    lv_obj_set_size(rects[i], lv_pct(100), 25);
    lv_obj_align(rects[i], LV_ALIGN_TOP_LEFT, 0, 0 + i * 25);
    lv_obj_align(names[i], LV_ALIGN_TOP_LEFT, 5, 5 + i * 25);
    lv_obj_align(dates[i], LV_ALIGN_TOP_RIGHT, -(5 - FNT_OFF), 5 + i * 25);
    lv_obj_set_style_border_width(rects[i], 0, LV_PART_MAIN);
    lv_obj_set_style_radius(rects[i], 0, LV_PART_MAIN);
  }

  // add signs
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_t open = {
      .title = "A",
      .text = scr_trans()->explore__open,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_create(&open, lv_scr_act());

  // add info
  lv_obj_t* info = lv_label_create(lv_scr_act());
  lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -5);

  // end draw
  gfx_end(true);

  for (;;) {
    // begin draw
    gfx_begin(false, false);

    // fill list
    for (int i = 0; i < +4; i++) {
      // get index
      int index = offset + i;

      // handle empty
      if (index >= total) {
        // clear labels and rectangle
        lv_label_set_text(names[i], "");
        lv_label_set_text(dates[i], "");
        lv_obj_set_style_bg_color(rects[i], lv_color_white(), LV_PART_MAIN);

        continue;
      }

      // get file
      dat_file_t* file = dat_get_file(index);

      // update labels
      lv_label_set_text(names[i], scr_file_name(file));
      lv_label_set_text(dates[i], scr_file_date(file));

      // handle selected
      if (index == selected) {
        lv_obj_set_style_text_color(names[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_color(dates[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(rects[i], lv_color_black(), LV_PART_MAIN);
      } else {
        lv_obj_set_style_text_color(names[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_color(dates[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(rects[i], lv_color_white(), LV_PART_MAIN);
      }
    }

    // update info
    lv_label_set_text(info, scr_fmt("%d/%d", selected + 1, (int)total));

    // end draw
    gfx_end(false);

    // await event
    sig_event_t event = sig_await(SIG_UP | SIG_DOWN | SIG_META | SIG_SCROLL, SCR_ACTION_TIMEOUT);

    // handle arrows
    if ((event.type & (SIG_UP | SIG_DOWN | SIG_SCROLL)) != 0) {
      if (event.type == SIG_SCROLL) {
        selected += (int)(event.touch * 2);
      } else {
        selected += event.type == SIG_UP ? -1 : 1;
      }
      while (selected < 0) {
        selected += (int)total;
      }
      while (selected > total - 1) {
        selected -= (int)total;
      }
      if (selected > offset + 3) {
        offset = selected - 3;
      } else if (selected < offset) {
        offset = selected;
      }
      continue;
    }

    /* handle meta and timeout */

    // cleanup
    scr_cleanup(false);

    // handle escape and timeout
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return scr_menu;
    }

    /* handle enter */

    // set file
    scr_file = selected;

    return scr_edit;
  }
}

static void* scr_usb() {
  // check recording
  if (rec_running()) {
    // show message
    scr_message(scr_trans()->usb__running, 2000);

    return scr_menu;
  }

  // check connection
  if (!al_power_get().usb) {
    // show message
    scr_message(scr_trans()->usb__disconnected, 2000);

    return scr_menu;
  }

  // set USB flag
  scr_led_flags |= SCR_LED_USB;

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
  gfx_end(false);

  // enable USB
  dat_enable_usb();

  // await escape
  sig_event_t event = sig_await(SIG_ESCAPE | SIG_EJECT, 0);

  // disable USB
  dat_disable_usb();

  // cleanup
  scr_cleanup(false);

  // clear USB flag
  scr_led_flags &= ~SCR_LED_USB;

  // show message on eject
  if (event.type == SIG_EJECT) {
    scr_message(scr_trans()->usb__eject, 2000);
  }

  return scr_menu;
}

static void* scr_reset() {
  // begin draw
  gfx_begin(false, true);

  // add text
  lv_obj_t* text = lv_label_create(lv_scr_act());
  lv_label_set_text(text, scr_trans()->reset__confirm);
  lv_obj_align(text, LV_ALIGN_TOP_MID, 0, 25);
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  // add signs
  lvx_sign_t next = {
      .title = "A",
      .text = scr_trans()->reset__yes,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->reset__no,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&next, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false);

  // await event
  sig_event_t event = sig_await(SIG_META, SCR_ACTION_TIMEOUT);

  // cleanup
  scr_cleanup(false);

  // handle escape
  if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
    return scr_settings;
  }

  /* handle enter */

  // reset data
  dat_reset();

  // show message
  scr_message(scr_trans()->reset__reset, 2000);

  // reset system
  esp_restart();

  return scr_intro;
}

static void* scr_settings() {
  // get storage info
  dat_info_t info = dat_info();

  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, scr_trans()->settings__title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);

  // add info
  lv_obj_t* version = lv_label_create(lv_scr_act());
  lv_label_set_text(version, "v" DEV_VERSION);
  lv_obj_align(version, LV_ALIGN_TOP_RIGHT, -5, 5);

  // add storage
  lv_obj_t* storage = lv_label_create(lv_scr_act());
  lv_label_set_text(storage, scr_fmt(scr_trans()->settings__storage, info.usage * 100.f));
  lv_obj_align(storage, LV_ALIGN_TOP_LEFT, 5, 26);

  // add signs
  lvx_sign_t datetime = {
      .title = "↑",
      .text = scr_trans()->settings__date_time,
      .align = LV_ALIGN_BOTTOM_LEFT,
      .offset = -25,
  };
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_t lang = {
      .title = "↓",
      .text = scr_trans()->settings__language,
      .align = LV_ALIGN_BOTTOM_RIGHT,
      .offset = -50,
  };
  lvx_sign_t off = {
      .title = ">",
      .text = scr_trans()->settings__off,
      .align = LV_ALIGN_BOTTOM_RIGHT,
      .offset = -25,
  };
  lvx_sign_t reset = {
      .title = "<",
      .text = scr_trans()->settings__reset,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_create(&datetime, lv_scr_act());
  lvx_sign_create(&reset, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&off, lv_scr_act());
  lvx_sign_create(&lang, lv_scr_act());

  // end draw
  gfx_end(false);

  for (;;) {
    // await event
    sig_type_t filter = SIG_UP | SIG_DOWN | SIG_LEFT | SIG_RIGHT | SIG_ESCAPE;
    sig_event_t event = sig_await(filter, SCR_ACTION_TIMEOUT);

    // cleanup
    scr_cleanup(false);

    // handle event
    switch (event.type) {
      case SIG_UP:
        return scr_date;
      case SIG_DOWN:
        return scr_language;
      case SIG_LEFT:
        return scr_reset;
      case SIG_RIGHT:
        scr_power_off();
        break;
      case SIG_ESCAPE:
      case SIG_TIMEOUT:
        // set action
        scr_action = STM_FROM_SETTINGS;

        return scr_menu;
      default:
        ESP_ERROR_CHECK(ESP_FAIL);
    }
  }
}

static void* scr_develop() {
  // prepare labels
  const char* labels[] = {
      "Light Sleep",  "Deep Sleep",    "Power Reset",  "Power Off",   "Ship Mode",
      "Screen Saver", "Clear Display", "Test Bubbles", "System Info", NULL,
  };

  // handle list
  int ret = 0;
  for (;;) {
    ret = scr_list(labels, "Select", "Cancel", ret);
    if (ret < 0) {
      return scr_menu;
    }

    // handle light/deep sleep
    if (ret == 0 || ret == 1) {
      // log sleep
      naos_log("sleeping... (deep=%d)", ret == 1);

      // set return
      scr_return_unlock = scr_develop;

      // perform sleep
      al_sleep(ret == 1, 0);

      // capture enter when unlocked
      if (al_trigger() == AL_BUTTON) {
        sig_await(SIG_ENTER, 1000);
      }

      // log wakeup
      naos_log("woke up!");
    }

    // handle power set
    if (ret == 2) {
      esp_restart();
    }

    // handle power off
    if (ret == 3) {
      scr_power_off();
    }

    // handle ship mode
    if (ret == 4) {
      // show message
      scr_write("Ship Mode\n\nConnect USB and\npress A to exit.");
      naos_delay(1000);

      // enable ship mode
      al_power_ship();

      // clean up in case ship mode did not work
      scr_cleanup(false);
    }

    // handle screen saver
    if (ret == 5) {
      // set return
      scr_return_unlock = scr_develop;

      // set enter
      scr_saver_enter = sys_get_timestamp();

      return scr_saver;
    }

    // handle clear display
    if (ret == 6) {
      scr_cleanup(true);
    }

    // handle bubbles test
    if (ret == 7) {
      return scr_test_bubbles;
    }

    // handle system info
    if (ret == 8) {
      return scr_info;
    }
  }
}

static void* scr_menu() {
  // prepare variables
  static int8_t mode = 0;  // co2, tmp, hum
  static int8_t opt = 0;   // create, explore, settings, usb, develop
  static bool fan_alt = false;

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
  gfx_end(true);

  // prepare deadline
  int64_t deadline = naos_millis() + SCR_IDLE_TIMEOUT;

  // prepare flags
  bool exclaim = true;
  bool fun = false;

  // prepare statement
  stm_entry_t* statement = NULL;

  for (;;) {
    // get time
    uint16_t hour, minute, seconds;
    sys_get_time(&hour, &minute, &seconds);

    // read sensor
    al_sensor_state_t sensor = al_sensor_get();

    // query sensor
    al_sensor_hist_t hist = al_sensor_query((al_sensor_mode_t)mode);

    // query statement
    if (statement == NULL && (exclaim || fun)) {
      statement = stm_query(exclaim, scr_action);
    }

    // begin draw
    gfx_begin(false, false);

    // update bar
    bar.time = scr_fmt("%02d:%02d", hour, minute);
    if (!sensor.ok) {
      bar.value = scr_trans()->menu__no_data;
    } else if (mode == 0) {
      bar.value = scr_fmt("%.0f ppm CO2", sensor.co2);
    } else if (mode == 1) {
      bar.value = scr_fmt("%.1f °C", sensor.tmp);
    } else if (mode == 2) {
      bar.value = scr_fmt("%.1f%% RH", sensor.hum);
    } else if (mode == 3) {
      bar.value = scr_fmt("%.0f VOC", sensor.voc);
    } else if (mode == 4) {
      bar.value = scr_fmt("%.0f NOx", sensor.nox);
    } else if (mode == 5) {
      bar.value = scr_fmt("%.0f hPa", sensor.prs);
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
      lv_img_set_src(icon, &img_wrench);
    }

    // set fan
    fan_alt = !fan_alt;
    if (fan_alt) {
      lv_img_set_src(fan, &img_fan2);
    } else {
      lv_img_set_src(fan, &img_fan1);
    }

    // draw chart
    lv_canvas_fill_bg(chart, lv_color_white(), LV_OPA_COVER);
    lv_point_t points[AL_SENSOR_HIST] = {0};
    for (size_t i = 0; i < AL_SENSOR_HIST; i++) {
      points[i].x = (lv_coord_t)a32_safe_map_i(i, 0, AL_SENSOR_HIST - 1, 0, 24);
      points[i].y = (lv_coord_t)a32_safe_map_f(hist.values[i], hist.min, hist.max, 14, 2);
    }
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = 2;
    lv_canvas_draw_line(chart, points, AL_SENSOR_HIST, &line_dsc);

    // draw drain
    lv_canvas_fill_bg(drain, lv_color_white(), LV_OPA_COVER);
    lv_coord_t drain_height = (lv_coord_t)a32_safe_map_f(hist.values[AL_SENSOR_HIST - 1], hist.min, hist.max, 0, 9);
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();
    lv_canvas_draw_rect(drain, 1, 1 + 9 - drain_height, 20, drain_height, &rect_dsc);

    // set bubble text
    switch (scr_lang) {
      case SCR_DE:
        bubble.text = statement ? statement->text_de : NULL;
        break;
      case SCR_EN:
        bubble.text = statement ? statement->text_en : NULL;
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
    gfx_end(false);

    // clear flags
    exclaim = false;
    fun = false;

    // await event
    sig_event_t event = sig_await(SIG_SENSOR | SIG_KEYS, 0);

    // handle deadline
    if (event.type == SIG_SENSOR && naos_millis() > deadline) {
      event.type = SIG_TIMEOUT;
    } else if (event.type != SIG_SENSOR) {
      deadline = naos_millis() + SCR_IDLE_TIMEOUT;
    }

    // clear statement on any key
    if (statement != NULL && (event.type & SIG_KEYS) != 0) {
      statement = NULL;
      continue;
    }

    // loop on sensor or scape
    if (event.type == SIG_SENSOR || event.type == SIG_ESCAPE) {
      // show fun fact after half of deadline expired
      if (deadline - naos_millis() < SCR_IDLE_TIMEOUT / 2) {
        fun = true;
      }

      continue;
    }

    // change mode on up/down
    if (event.type == SIG_UP) {
      mode++;
      if (mode > 5) {
        mode = 0;
      }
      continue;
    } else if (event.type == SIG_DOWN) {
      mode--;
      if (mode < 0) {
        mode = 5;
      }
      continue;
    }

    // change opt left/right
    if (event.type == SIG_LEFT) {
      opt--;
      if (opt < 0) {
        opt = 4;
      }
      continue;
    } else if (event.type == SIG_RIGHT) {
      opt++;
      if (opt > 4) {
        opt = 0;
      }
      continue;
    }

    // cleanup
    scr_cleanup(false);

    // clear action
    scr_action = 0;

    // enter screen saver on timeout
    if (event.type == SIG_TIMEOUT) {
      // set return
      scr_return_unlock = scr_menu;

      // set enter
      scr_saver_enter = sys_get_timestamp();

      return scr_saver;
    }

    // handle enter
    if (event.type == SIG_ENTER) {
      switch (opt) {
        case 0:  // create or view
          if (rec_running()) {
            scr_file = rec_file();
            return scr_view;
          } else {
            return scr_create;
          }
        case 1:  // explore
          return scr_explore;
        case 2:  // settings
          return scr_settings;
        case 3:  // usb
          return scr_usb;
        case 4:  // develop
          return scr_develop;
        default:
          ESP_ERROR_CHECK(ESP_FAIL);
      }
    }
  }
}

static void* scr_time() {
  // show message
  scr_message(scr_trans()->time__message, 3000);

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
  uint16_t seconds;
  sys_get_time(&hour.value, &minute.value, &seconds);

  // add wheels
  lvx_wheel_create(&hour, row);
  lvx_wheel_create(&minute, row);

  // add button
  lvx_sign_t back = {
      .title = "B",
      .text = scr_trans()->back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_t next = {
      .title = "A",
      .text = scr_trans()->next,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&next, lv_scr_act());

  // end draw
  gfx_end(false);

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_KEYS | SIG_SCROLL, SCR_ACTION_TIMEOUT);

    // forward arrows
    if ((event.type & (SIG_ARROWS | SIG_SCROLL)) != 0) {
      lvx_handle(event, true);
      continue;
    }

    // cleanup
    scr_cleanup(false);

    // handle escape/timeout event
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return scr_date;
    }

    /* handle enter */

    // save time
    sys_set_time(hour.value, minute.value, 0);

    // update clock
    al_clock_update();

    // show message
    scr_message(scr_trans()->time__continue, 5000);

    // section action
    scr_action = STM_FROM_INTRO;

    return scr_menu;
  }
}

static void* scr_date() {
  // show message
  scr_message(scr_trans()->date__message, 5000);

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
  lvx_wheel_t day = {.value = 15, .min = 1, .max = 31, .format = "%02d", .fixed = true};
  lvx_wheel_t month = {.value = 6, .min = 1, .max = 12, .format = "%02d", .fixed = true};
  lvx_wheel_t year = {.value = 2023, .min = 2023, .max = 2999, .fixed = true};

  // assign current date
  sys_get_date(&year.value, &month.value, &day.value);

  // add wheels
  lvx_wheel_create(&day, row);
  lvx_wheel_create(&month, row);
  lvx_wheel_create(&year, row);

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

  // end draw
  gfx_end(false);

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_KEYS | SIG_SCROLL, SCR_ACTION_TIMEOUT);

    // handle arrows
    if ((event.type & (SIG_ARROWS | SIG_SCROLL)) != 0) {
      lvx_handle(event, true);
      continue;
    }

    // cleanup
    scr_cleanup(false);

    // return on escape/timeout
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return scr_settings;
    }

    /* handle enter */

    // save date
    sys_set_date(year.value, month.value, day.value);

    // update clock
    al_clock_update();

    return scr_time;
  }
}

static void* scr_language() {
  // show message
  scr_message(scr_trans()->language__message, 5000);

  // prepare labels
  const char* labels[] = {"Deutsch", "English", NULL};

  // add row
  int ret = scr_list(labels, "Select", "Cancel", scr_lang);
  if (ret < 0) {
    return scr_settings;
  }

  // set language
  scr_lang = ret;

  return scr_settings;
}

static void* scr_intro() {
  // wait a bit
  naos_delay(1000);

  // show robin
  gfx_begin(false, false);
  lv_obj_t* img = lv_img_create(lv_scr_act());
  lv_img_set_src(img, &img_robin_standing);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
  gfx_end(false);

  // wait a bit
  naos_delay(2000);

  // show text
  gfx_begin(false, false);
  lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* lbl = lv_label_create(lv_scr_act());
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_line_space(lbl, 6, LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_text(lbl, scr_trans()->intro_message);
  gfx_end(false);

  // wait a bit
  naos_delay(3000);

  // cleanup
  scr_cleanup(false);

  return scr_date;
}

/* Management */

static void scr_task() {
  // prepare handler
  void* (*handler)() = scr_menu;

  // handle return
  al_trigger_t trigger = al_trigger();
  if ((trigger == AL_BUTTON || trigger == AL_MOTION) && scr_return_unlock != NULL) {
    handler = scr_return_unlock;
  } else if (trigger == AL_TIMEOUT && scr_return_timeout != NULL) {
    handler = scr_return_timeout;
  }

  // call handlers
  for (;;) {
    void* next = handler();
    handler = next;
  }
}

void scr_led() {
  // handle off
  if (scr_led_flags & SCR_LED_OFF) {
    al_led_set(0, 0, 0);
    return;
  }

  // get power state
  al_power_state_t state = al_power_get();

  // get recording state
  bool recording = rec_running();

  // determine color
  float r = 0, g = 0, b = 0;
  if (scr_led_flags & SCR_LED_USB) {
    r = .8f;
    b = .7f;
  } else if (recording) {
    r = .8f;
    g = .02f;
    b = .02f;
  } else {
    r = .7f;
    g = .15f;
    b = .2f;
  }

  // set LED
  if (state.usb) {
    al_led_set(r, g, b);
  } else {
    al_led_flash(r, g, b);
  }
}

void scr_run() {
  // run screen task
  naos_run("scr", 8192, 1, scr_task);

  // run led control
  naos_repeat("led", 500, scr_led);
}
