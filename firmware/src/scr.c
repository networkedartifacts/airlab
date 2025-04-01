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
#include <al/power.h>
#include <al/clock.h>
#include <al/sensor.h>

#include "gui.h"
#include "gfx.h"
#include "sig.h"
#include "fnt.h"
#include "img.h"
#include "lvx.h"
#include "rec.h"
#include "dev.h"
#include "stm.h"
#include "hmi.h"

#define SCR_ACTION_TIMEOUT 10000
#define SCR_IDLE_TIMEOUT 30000
#define SCR_MIN_RESOLUTION 5000
#define SCR_HIST_POINTS 8

static stm_action_t scr_action = 0;
DEV_KEEP static uint16_t scr_file = 0;
DEV_KEEP static int64_t scr_saver_enter = 0;
DEV_KEEP static void* scr_return_timeout = NULL;
DEV_KEEP static void* scr_return_unlock = NULL;

static const char* scr_field_fmt[] = {
    [AL_SAMPLE_CO2] = "%.0f ppm CO2", [AL_SAMPLE_TMP] = "%.1f °C",  [AL_SAMPLE_HUM] = "%.1f %% RH",
    [AL_SAMPLE_VOC] = "%.0f VOC",     [AL_SAMPLE_NOX] = "%.0f NOx", [AL_SAMPLE_PRS] = "%.0f hPa",
};

/* Helpers */

static const char* scr_ms2str(int32_t ms) {
  if (ms > 1000 * 60 * 60) {  // hours
    return lvx_fmt("%dh", ms / 1000 / 60 / 60);
  } else if (ms > 1000 * 60) {  // minutes
    return lvx_fmt("%dm", ms / 1000 / 60);
  } else {  // seconds
    return lvx_fmt("%ds", ms / 1000);
  }
}

static void scr_power_off() {
  // set off flag
  hmi_set_flag(HMI_FLAG_OFF);

  // cleanup screen
  gui_cleanup(true);

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
  const char* yes;
  const char* no;
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
  const char* create__import;
  const char* create__importing;
  const char* create__imported;
  const char* delete__confirm;
  const char* delete__delete;
  const char* delete__deleted;
  const char* edit__analyse;
  const char* edit__delete;
  const char* edit__export;
  const char* edit__export_fail;
  const char* edit__export_done;
  const char* explore__empty;
  const char* explore__open;
  const char* usb__running;
  const char* usb__disconnected;
  const char* usb__active;
  const char* usb__eject;
  const char* reset__confirm;
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
            .yes = "Ja",
            .no = "Nein",
            .back = "Zurück",
            .next = "Weiter",
            .cancel = "Abbrechen",
            .measurement = "Messung %u",
            .exit__stop = "Messung beenden",
            .exit__back = "Zurück zum Labor",
            .exit__stopped = "%s\n beendet!",
            .create__full = "Speicher voll!",
            .create__new = "Neue Messung erstellen?",
            .create__name = "Messung %u",
            .create__length = "Länge ca. %d-%d Stunden",
            .create__start = "Starten",
            .create__import = "Bestehende Daten importieren?",
            .create__importing = "Importiere Daten...",
            .create__imported = "Import erfolgreich!",
            .delete__confirm = "%s\nwirklich löschen?",
            .delete__delete = "Löschen",
            .delete__deleted = "Messung %d\nerfolgreich gelöscht!",
            .edit__analyse = "Analysieren",
            .edit__delete = "Löschen",
            .edit__export = "CSV Exportieren",
            .edit__export_fail = "Export fehlgeschlagen!",
            .edit__export_done = "Export erfolgreich!",
            .explore__empty = "Keine gespeicherte\nMessungen...",
            .explore__open = "Öffnen",
            .usb__running = "Messung läuft!",
            .usb__disconnected = "USB nicht angeschlossen!",
            .usb__active = "USB-Modus Aktiv",
            .usb__eject = "USB-Verbindung getrennt",
            .reset__confirm = "Air Lab\nwirklich zurücksetzen?",
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
            .yes = "Yes",
            .no = "No",
            .back = "Back",
            .next = "Next",
            .cancel = "Cancel",
            .measurement = "Measurement %u",
            .exit__stop = "Stop Measurement",
            .exit__back = "Go back to Lab",
            .exit__stopped = "%s\n stopped!",
            .create__full = "Storage full!",
            .create__new = "Create new measurement?",
            .create__name = "Measurement %u",
            .create__length = "Length approx. %d-%d hours",
            .create__start = "Start",
            .create__import = "Import existing data?",
            .create__importing = "Importing data...",
            .create__imported = "Import successful!",
            .delete__confirm = "Really delete %s?",
            .delete__delete = "Delete",
            .delete__deleted = "Measurement %d\nsuccessfully deleted!",
            .edit__analyse = "Analyse",
            .edit__delete = "Delete",
            .edit__export = "Export CSV",
            .edit__export_fail = "Export failed!",
            .edit__export_done = "Export done!",
            .explore__empty = "No saved\nmeasurements...",
            .explore__open = "Open",
            .usb__running = "Measurement running!",
            .usb__disconnected = "USB not connected!",
            .usb__active = "USB Mode Active",
            .usb__eject = "USB-Connection disconnected",
            .reset__confirm = "Fully Reset Air Lab?",
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

static const scr_trans_t* scr_trans() {
  // return translation
  return &scr_trans_map[scr_lang];
}

/* Formatters */

static const char* scr_file_name(dat_file_t* file) {
  // return name
  return lvx_fmt(scr_trans()->measurement, file->head.num);
}

static const char* scr_file_date(dat_file_t* file) {
  static char buf[24];

  // format date
  time_t time = (time_t)(file->head.start / 1000);
  struct tm ts = *gmtime(&time);
  strftime(buf, sizeof(buf), "%d.%m.%Y", &ts);
  return buf;
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
static void* scr_develop();
static void* scr_date();
static void* scr_language();

static void* scr_bubbles() {
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
  gfx_end(true, false);

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
    gfx_end(false, false);

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
    gui_cleanup(false);

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
                               naos_millis() / 1000, bat.battery * 100, bat.usb, bat.fast, year, month, day, hour,
                               minute, seconds, esp_get_free_heap_size() / 1024, cpu0 * 100, cpu1 * 100);

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
    // get power
    size_t num_5s = al_sensor_count(AL_SENSOR_5S);
    size_t num_30s = al_sensor_count(AL_SENSOR_30S);

    // prepare text
    const char* text = lvx_fmt("5s: %d / %d\n30s: %d / %d", num_5s, AL_SENSOR_NUM_5S, num_30s, AL_SENSOR_NUM_30S);

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

static void* scr_saver() {
  // prepare variables
  DEV_KEEP static bool right = true;

  // set timeout return
  scr_return_timeout = scr_saver;

  // begin draw
  gfx_begin(false, false);

  // add status
  lvx_status_t status = {0};
  lvx_status_create(&status, lv_scr_act());

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
  gfx_end(true, false);

  for (;;) {
    // get time
    uint16_t hour, minute, seconds;
    al_clock_get_time(&hour, &minute, &seconds);

    // get last sample
    al_sample_t sample = al_sensor_last();

    // await sample, if invalid (after reset)
    if (!al_sample_valid(sample)) {
      sig_await(SIG_SENSOR, 0);
      sample = al_sensor_last();
    }

    // read power state
    al_power_state_t power = al_power_get();

    // get accelerometer state
    al_accel_state_t acc = al_accel_get();

    // flip side
    right = !right;

    // begin draw
    gfx_begin(false, false);

    // determine vertical
    bool vertical = acc.rotation == 90 || acc.rotation == 270;

    // set display rotation
    lv_disp_set_rotation(NULL, acc.rotation / 90);

    // update status
    lvx_status_update(&status);

    // TODO: Show VOC, NOx and pressure.

    // update values
    lv_label_set_text(time, lvx_fmt("%02d:%02d", hour, minute));
    if (vertical) {
      lv_label_set_text(co2, "ppm CO2");
      lv_label_set_text(tmp, "° Celsius");
      lv_label_set_text(hum, "% RH");
      lv_label_set_text(co2_big, lvx_fmt("%.0f", al_sample_read(sample, AL_SAMPLE_CO2)));
      lv_label_set_text(tmp_big, lvx_fmt("%.1f", al_sample_read(sample, AL_SAMPLE_TMP)));
      lv_label_set_text(hum_big, lvx_fmt("%.1f", al_sample_read(sample, AL_SAMPLE_HUM)));
    } else {
      lv_label_set_text(co2, lvx_fmt("%.0f ppm", al_sample_read(sample, AL_SAMPLE_CO2)));
      lv_label_set_text(tmp, lvx_fmt("%.1f °C", al_sample_read(sample, AL_SAMPLE_TMP)));
      lv_label_set_text(hum, lvx_fmt("%.1f%% RH", al_sample_read(sample, AL_SAMPLE_HUM)));
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
      lv_obj_align(status.row, LV_ALIGN_BOTTOM_LEFT, 25, -25);
    } else {
      lv_align_t align = right ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT;
      lv_obj_align(status.row, align, right ? -20 : 20, 19);
      lv_obj_align(time, align, right ? -19 : 19, 41);
      lv_obj_align(co2, align, right ? -19 : 19, 59);
      lv_obj_align(tmp, align, right ? -19 : 19, 77);
      lv_obj_align(hum, align, right ? -19 : 19, 95);
      lv_obj_align(co2_big, 0, -100, -100);
      lv_obj_align(tmp_big, 0, -100, -100);
      lv_obj_align(hum_big, 0, -100, -100);
    }

    // end draw
    gfx_end(false, true);

    /* Sleep Control */

    // determine duration
    int64_t duration = al_clock_get_epoch() - scr_saver_enter;

    // power off if battery is low and not charging
    if (power.battery < 0.10 && !power.usb && !power.charging) {
      scr_power_off();
    }

    // check if recording
    if (!rec_running()) {
      if (power.usb) {
        // wait one second
        sig_event_t event = sig_await(SIG_KEYS | SIG_TIMEOUT | SIG_MOTION, 60 * 1000);

        // handle unlock
        if (event.type & SIG_KEYS) {
          break;
        }

        continue;
      } else {
        // sleep for one second (no return)
        al_sleep(true, 60 * 1000);
      }
    }

    // calculate timeout: 5s-30s (0-5min)
    int64_t timeout = a32_safe_map_l(duration, 0, 300000, 5000, 30000);

    // check if powered
    if (power.usb) {
      // wait some time
      sig_event_t event = sig_await(SIG_KEYS | SIG_TIMEOUT | SIG_MOTION, timeout);

      // handle unlock
      if (event.type & SIG_KEYS) {
        break;
      }
    } else {
      // TODO: Also use deep sleep here?

      // light sleep for some time
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
    }

    // await next measurement or stop
    sig_await(SIG_APPEND | SIG_STOP, 0);
  }

  // cleanup
  gui_cleanup(false);

  return scr_return_unlock;
}

static void* scr_view() {
  // prepare variables
  static int8_t mode = 0;  // co2, tmp, hum, voc, nox, prs
  static bool advanced = false;
  static al_sample_t samples[LVX_CHART_SIZE];

  // zero samples
  memset(samples, 0, sizeof(samples));

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
  static lv_color_t chart_buffer[LV_CANVAS_BUF_SIZE_TRUE_COLOR(288, 96)] = {0};
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
    source = al_sensor_source();
  } else {
    source = dat_source(scr_file);
  }

  // prepare position
  int32_t position = 0;
  if (!recording) {
    position = source.stop(source.ctx) / 2;
  }

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
    } else if (advanced) {
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
    } else if (advanced) {
      start = position - LVX_CHART_SIZE / 2 * resolution;
      end = position + LVX_CHART_SIZE / 2 * resolution;
      if (start < 0) {
        end += start * -1;
        start = 0;
      }
      if (end > source_stop) {
        int32_t shift = fminf(start, end - source_stop);
        end -= shift;
        start -= shift;
      }
    }

    // calculate index
    size_t index = roundf(a32_safe_map_f(position, start, end, 0, LVX_CHART_SIZE - 1));

    // TODO: Only query needed dimension.

    // query samples
    if (source_count > 0) {
      size_t num = al_sample_query(&source, samples, LVX_CHART_SIZE, start, resolution);
      if (recording) {
        index = num - 1;
      }
    }

    // find marks
    uint8_t marks[LVX_CHART_SIZE] = {0};
    if (file != NULL) {
      for (uint8_t i = 0; i < DAT_MARKS; i++) {
        if (file->head.marks[i] > 0) {
          int32_t mark = roundf(a32_map_f(file->head.marks[i], start, end, 0, LVX_CHART_SIZE - 1));
          if (mark >= 0 && mark <= LVX_CHART_SIZE - 1) {
            marks[(size_t)mark] = i + 1;
          }
        }
      }
    }

    // select current sample
    al_sample_t current = samples[index];

    // parse time
    uint16_t hour;
    uint16_t minute;
    al_clock_conv_epoch(source_start + (int64_t)current.off, &hour, &minute, NULL);

    // begin draw
    gfx_begin(false, advanced);

    // update bar
    bar.time = lvx_fmt("%02d:%02d", hour, minute);
    if (file != NULL) {
      if (recording) {
        bar.mark = file->marks > 0 ? lvx_fmt("(M%d)", file->marks) : "";
      } else {
        bar.mark = marks[index] > 0 ? lvx_fmt("(M%d)", marks[index]) : "";
      }
    }
    bar.value = lvx_fmt(scr_field_fmt[mode], al_sample_read(current, mode));
    lvx_bar_update(&bar);

    // prepare range
    float range = 100;  // tmp, hum, nox
    if (mode == 0) {
      range = 3000;  // co2
    } else if (mode == 3) {
      range = 500;  // voc
    } else if (mode == 5) {
      range = 1500;  // prs
    }

    // collect values
    float values[LVX_CHART_SIZE];
    for (size_t i = 0; i < LVX_CHART_SIZE; i++) {
      al_sample_t sample = samples[i];
      values[i] = al_sample_read(sample, mode);
      if (values[i] > range) {
        range = values[i];
      }
    }

    // draw chart
    lvx_chart_draw((lvx_chart_t){
        .canvas = canvas,
        .range = range,
        .values = values,
        .marks = marks,
        .arrows = advanced,
        .offset = source_start,
        .start = start,
        .end = end,
        .stop = source_stop,
        .cursor = !recording,
        .index = index,
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
    sig_event_t event = sig_await(filter, SCR_IDLE_TIMEOUT);

    // handle deadline
    if (event.type & (SIG_SENSOR | SIG_APPEND) && naos_millis() > deadline) {
      event.type = SIG_TIMEOUT;
    } else if (event.type & (SIG_KEYS | SIG_SCROLL)) {
      deadline = naos_millis() + SCR_IDLE_TIMEOUT;
    }

    // update on append or stop
    if (event.type & (SIG_SENSOR | SIG_APPEND | SIG_STOP)) {
      continue;
    }

    // handle idle timeout
    if (event.type == SIG_TIMEOUT) {
      // cleanup
      gui_cleanup(false);

      // set return
      scr_return_unlock = scr_view;

      // set enter
      scr_saver_enter = al_clock_get_epoch();

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
      gui_cleanup(false);

      // handle recording
      if (recording) {
        // choose option
        int ret = gui_choose(scr_trans()->exit__stop, scr_trans()->exit__back, true, SCR_ACTION_TIMEOUT);
        if (ret == 0) {
          return scr_view;
        }

        // set action
        if (scr_action == 0) {
          scr_action = STM_FROM_MEASUREMENT;
        }

        // handle stop
        if (ret == 1) {
          // stop recording
          rec_stop();

          // show message
          gui_message(lvx_fmt(scr_trans()->exit__stopped, scr_file_name(file)), 2000);

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

    // change position on left/right/scroll if not recording
    if (!recording) {
      if (event.type == SIG_LEFT) {
        position -= resolution * (event.repeat ? 5 : 1);
      } else if (event.type == SIG_RIGHT) {
        position += resolution * (event.repeat ? 5 : 1);
      } else if (event.type == SIG_SCROLL) {
        position += resolution * (int32_t)(event.touch * 2);
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
    gui_message(scr_trans()->create__full, 2000);
    return scr_explore;
  }

  // calculate min and max time
  uint32_t min_hours = samples / 12 / 60;  // 12 samples per minute
  uint32_t max_hours = samples / 2 / 60;   // 2 samples per minute

  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, scr_trans()->create__new);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);

  // add name
  lv_obj_t* name = lv_label_create(lv_scr_act());
  lv_label_set_text(name, lvx_fmt(scr_trans()->create__name, dat_next()));
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 5, 26);

  // add mode
  lv_obj_t* mode = lv_label_create(lv_scr_act());
  lv_label_set_text(mode, "CO2, TMP, RH, VOC, NOx, PRS");
  lv_obj_align(mode, LV_ALIGN_TOP_LEFT, 5, 47);

  // add length
  lv_obj_t* length = lv_label_create(lv_scr_act());
  lv_label_set_text(length, lvx_fmt(scr_trans()->create__length, min_hours, max_hours));
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
  gfx_end(false, false);

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_META, SCR_ACTION_TIMEOUT);

    // cleanup
    gui_cleanup(false);

    // handle escape and timeout
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return scr_explore;
    }

    /* handle enter */

    // confirm import
    bool import =
        gui_confirm(scr_trans()->create__import, scr_trans()->yes, scr_trans()->no, false, SCR_ACTION_TIMEOUT);

    // determine epoch
    int64_t epoch = al_clock_get_epoch();
    if (import) {
      al_sample_source_t source = al_sensor_source();
      epoch = source.start(source.ctx);
    }

    // create measurement
    scr_file = dat_create(epoch);

    // confirm and perform data import
    if (import) {
      // write message
      gui_write(scr_trans()->create__importing);

      // set flag
      hmi_set_flag(HMI_FLAG_PROCESS);

      // perform import
      dat_import(scr_file);

      // clear flag
      hmi_clear_flag(HMI_FLAG_PROCESS);

      // write message
      gui_cleanup(false);
      gui_message(scr_trans()->create__imported, 2000);
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
}

static void* scr_edit() {
  // begin draw
  gfx_begin(false, false);

  // find file
  dat_file_t* file = dat_find(scr_file, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
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
      gui_message(lvx_fmt(scr_trans()->delete__deleted, num), 2000);

      return scr_explore;
    }

    // handle export
    if (event.type == SIG_RIGHT) {
      // set flag
      hmi_set_flag(HMI_FLAG_PROCESS);

      // perform export
      bool ok = dat_export(scr_file);

      // clear flag
      hmi_clear_flag(HMI_FLAG_PROCESS);

      // export file
      if (!ok) {
        gui_message(scr_trans()->edit__export_fail, 2000);
      } else {
        gui_message(scr_trans()->edit__export_done, 2000);
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
        .title = "Create Measurement",
        .info = "",
    };
  }

  // get file
  dat_file_t* file = dat_get(num - 1);

  return (gui_list_item_t){
      .title = scr_file_name(file),
      .info = scr_file_info(file),
  };
}

static void* scr_explore() {
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
  int ret = gui_list((int)total + 1, start, scr_trans()->explore__open, scr_trans()->back, scr_explore_cb, NULL,
                     SCR_ACTION_TIMEOUT);
  if (ret < 0) {
    return scr_menu;
  }

  // handle create
  if (ret == 0) {
    scr_file = 0;
    return scr_create;
  }

  // set file
  scr_file = dat_get(ret - 1)->head.num;

  return scr_edit;
}

static void* scr_usb() {
  // check recording
  if (rec_running()) {
    // show message
    gui_message(scr_trans()->usb__running, 2000);

    return scr_menu;
  }

  // check connection
  if (!al_power_get().usb) {
    // show message
    gui_message(scr_trans()->usb__disconnected, 2000);

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
    gui_message(scr_trans()->usb__eject, 2000);
  }

  return scr_menu;
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
  lv_label_set_text(storage, lvx_fmt(scr_trans()->settings__storage, info.usage * 100.f));
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
  gfx_end(false, false);

  for (;;) {
    // await event
    sig_type_t filter = SIG_UP | SIG_DOWN | SIG_LEFT | SIG_RIGHT | SIG_ESCAPE;
    sig_event_t event = sig_await(filter, SCR_ACTION_TIMEOUT);

    // cleanup
    gui_cleanup(false);

    // handle reset
    if (event.type == SIG_LEFT) {
      // confirm reset
      if (!gui_confirm(scr_trans()->reset__confirm, scr_trans()->yes, scr_trans()->no, true, SCR_ACTION_TIMEOUT)) {
        return scr_settings;
      }

      // reset data
      dat_reset();

      // show message
      gui_message(scr_trans()->reset__reset, 2000);

      // restart device
      esp_restart();
    }

    // handle event
    switch (event.type) {
      case SIG_UP:
        return scr_date;
      case SIG_DOWN:
        return scr_language;
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
      "Light Sleep",   "Deep Sleep",   "Power Reset", "Power Off",   "Ship Mode", "Screen Saver",
      "Clear Display", "Test Bubbles", "System Info", "Sensor Data", NULL,
  };

  // handle list
  int ret = 0;
  for (;;) {
    ret = gui_list_strings(ret, labels, "Select", "Cancel", SCR_ACTION_TIMEOUT);
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
      gui_write("Ship Mode\n\nConnect USB and\npress A to exit.");
      naos_delay(1000);

      // enable ship mode
      al_power_ship();

      // clean up in case ship mode did not work
      gui_cleanup(false);
    }

    // handle screen saver
    if (ret == 5) {
      // set return
      scr_return_unlock = scr_develop;

      // set enter
      scr_saver_enter = al_clock_get_epoch();

      return scr_saver;
    }

    // handle clear display
    if (ret == 6) {
      gui_cleanup(true);
    }

    // handle bubbles test
    if (ret == 7) {
      return scr_bubbles;
    }

    // handle system info
    if (ret == 8) {
      return scr_info;
    }

    // handle sensor data
    if (ret == 9) {
      return scr_sensor;
    }
  }
}

static void* scr_menu() {
  // prepare variables
  static int8_t mode = 0;  // co2, tmp, hum, voc, nox, prs
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
  gfx_end(true, false);

  // prepare deadline
  int64_t deadline = naos_millis() + SCR_IDLE_TIMEOUT;

  // prepare flags
  bool exclaim = true;
  bool fun = false;

  // prepare statement
  stm_entry_t* statement = NULL;

  // prepare sample source
  al_sample_source_t source = al_sensor_source();

  for (;;) {
    // get time
    uint16_t hour, minute, seconds;
    al_clock_get_time(&hour, &minute, &seconds);

    // get last sample
    al_sample_t sample = al_sensor_last();

    // query sensor
    float values[SCR_HIST_POINTS] = {0};
    float min = 0, max = 0;
    al_sample_pick(&source, (al_sample_field_t)mode, SCR_HIST_POINTS, values, &min, &max);

    // query statement
    if (statement == NULL && (exclaim || fun)) {
      statement = stm_query(exclaim, scr_action);
    }

    // begin draw
    gfx_begin(false, false);

    // update bar
    bar.time = lvx_fmt("%02d:%02d", hour, minute);
    if (!al_sample_valid(sample)) {
      bar.value = scr_trans()->menu__no_data;
    } else {
      bar.value = lvx_fmt(scr_field_fmt[mode], al_sample_read(sample, mode));
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
    lv_point_t points[SCR_HIST_POINTS] = {0};
    for (size_t i = 0; i < SCR_HIST_POINTS; i++) {
      points[i].x = (lv_coord_t)a32_safe_map_i(i, 0, SCR_HIST_POINTS - 1, 0, 24);
      points[i].y = (lv_coord_t)a32_safe_map_f(values[i], min, max, 14, 2);
    }
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = 2;
    lv_canvas_draw_line(chart, points, SCR_HIST_POINTS, &line_dsc);

    // draw drain
    lv_canvas_fill_bg(drain, lv_color_white(), LV_OPA_COVER);
    lv_coord_t drain_height = (lv_coord_t)a32_safe_map_f(values[SCR_HIST_POINTS - 1], min, max, 0, 9);
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
    gfx_end(false, false);

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
    gui_cleanup(false);

    // clear action
    scr_action = 0;

    // enter screen saver on timeout
    if (event.type == SIG_TIMEOUT) {
      // set return
      scr_return_unlock = scr_menu;

      // set enter
      scr_saver_enter = al_clock_get_epoch();

      return scr_saver;
    }

    // handle enter
    if (event.type == SIG_ENTER) {
      switch (opt) {
        case 0:  // view recording or live
          scr_file = rec_running() ? rec_file() : 0;
          return scr_view;
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
  gui_message(scr_trans()->time__message, 3000);

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
  al_clock_get_time(&hour.value, &minute.value, &seconds);

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
  gfx_end(false, false);

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_KEYS | SIG_SCROLL, SCR_ACTION_TIMEOUT);

    // forward arrows
    if ((event.type & (SIG_ARROWS | SIG_SCROLL)) != 0) {
      lvx_handle(event, true);
      continue;
    }

    // cleanup
    gui_cleanup(false);

    // handle escape/timeout event
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return scr_date;
    }

    /* handle enter */

    // save time
    al_clock_set_time(hour.value, minute.value, 0);

    // show message
    gui_message(scr_trans()->time__continue, 5000);

    // section action
    scr_action = STM_FROM_INTRO;

    return scr_menu;
  }
}

static void* scr_date() {
  // show message
  gui_message(scr_trans()->date__message, 5000);

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
  al_clock_get_date(&year.value, &month.value, &day.value);

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
  gfx_end(false, false);

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_KEYS | SIG_SCROLL, SCR_ACTION_TIMEOUT);

    // handle arrows
    if ((event.type & (SIG_ARROWS | SIG_SCROLL)) != 0) {
      lvx_handle(event, true);
      continue;
    }

    // cleanup
    gui_cleanup(false);

    // return on escape/timeout
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return scr_settings;
    }

    /* handle enter */

    // save date
    al_clock_set_date(year.value, month.value, day.value);

    return scr_time;
  }
}

static void* scr_language() {
  // show message
  gui_message(scr_trans()->language__message, 5000);

  // prepare labels
  const char* labels[] = {"Deutsch", "English", NULL};

  // add row
  int ret = gui_list_strings(scr_lang, labels, "Select", "Cancel", SCR_ACTION_TIMEOUT);
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
  gfx_end(false, false);

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
  gfx_end(false, false);

  // wait a bit
  naos_delay(3000);

  // cleanup
  gui_cleanup(false);

  return scr_date;
}

/* Management */

static void scr_task() {
  // prepare handler
  void* (*handler)() = scr_menu;

  // handle return
  al_trigger_t trigger = al_trigger();
  if (trigger == AL_BUTTON && scr_return_unlock != NULL) {
    handler = scr_return_unlock;
  } else if ((trigger == AL_TIMEOUT || trigger == AL_MOTION) && scr_return_timeout != NULL) {
    handler = scr_return_timeout;
  }

  // call handlers
  for (;;) {
    void* next = handler();
    handler = next;
  }
}

void scr_run() {
  // run screen task
  naos_run("scr", 8192, 1, scr_task);
}
