#include <naos.h>
#include <naos_sys.h>
#include <art32/numbers.h>
#include <lvgl.h>
#include <math.h>

#include "gfx.h"
#include "sig.h"
#include "sns.h"
#include "pwr.h"
#include "fnt.h"
#include "img.h"
#include "lvx.h"
#include "sys.h"
#include "rec.h"
#include "epd.h"
#include "dev.h"
#include "stm.h"

#define SCR_ACTION_TIMEOUT 10000
#define SCR_IDLE_TIMEOUT 30000
#define SCR_CHART_POINTS 72
#define SCR_POSITION_STEP 300000

static stm_action_t scr_action = 0;
static dat_file_t* scr_file = NULL;
static dat_point_t scr_points[SCR_CHART_POINTS] = {0};
DEV_KEEP static void* scr_return = NULL;

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

static const char* scr_ms2ts(int32_t ms) {
  int32_t hours = ms / 3600000;
  int32_t minutes = (ms / 60000) % 60;
  return scr_fmt("%02d:%02d", hours, minutes);
}

static void scr_cleanup(bool refresh) {
  // clear group and screen
  gfx_begin(refresh, false);
  lv_group_remove_all_objs(gfx_get_group());
  lv_obj_clean(lv_scr_act());
  gfx_end();
}

static void scr_message(const char* text) {
  // show heart and title
  gfx_begin(false, false);
  lv_obj_t* lbl = lv_label_create(lv_scr_act());
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_line_space(lbl, 6, LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  gfx_end();

  // wait some time
  naos_delay(2000);

  // cleanup
  scr_cleanup(false);
}

static void scr_power_off() {
  // cleanup screen
  scr_cleanup(true);
  naos_delay(5000);

  // clear return
  scr_return = NULL;

  // power off
  pwr_off();
  naos_delay(5000);
}

/* Screens */

static void* scr_debug();
static void* scr_saver();
static void* scr_view();
static void* scr_edit();
static void* scr_explore();
static void* scr_menu();
static void* scr_settings();
static void* scr_date();
static void* scr_intro();

static void* scr_test() {
  // begin draw
  gfx_begin(false, false);

  // add bubble
  lvx_bubble_t bubble = {};
  lvx_bubble_create(&bubble, lv_scr_act());

  // add signs
  lvx_sign_t back = {.title = "B", .text = "Back", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_t next = {.title = ">", .text = "Next", .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&next, lv_scr_act());

  // end draw
  gfx_end();

  // prepare index
  int index = 0;

  for (;;) {
    // begin draw
    gfx_begin(false, false);

    // set bubble
    bubble.text = stm_get(index)->text;
    lvx_bubble_update(&bubble);

    // end draw
    gfx_end();

    // await event
    sig_event_t event = sig_await(SIG_ESCAPE | SIG_RIGHT, 0);

    // handle right
    if (event == SIG_RIGHT) {
      index++;
      if (!stm_get(index)) {
        index = 0;
      }
      continue;
    }

    /* handle escape */

    // cleanup screen
    scr_cleanup(false);

    return scr_debug;
  }
}

static void* scr_debug() {
  // begin draw
  gfx_begin(false, false);

  // add label
  lv_obj_t* label = lv_label_create(lv_scr_act());
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(label, 6, LV_PART_MAIN);

  // add signs
  lvx_sign_t start = {.title = "B", .text = "Menu", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_t white = {.title = "A", .text = "Test", .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_t light = {.title = "↑", .text = "Light", .align = LV_ALIGN_BOTTOM_LEFT, .offset = -50};
  lvx_sign_t deep = {.title = "↓", .text = "Deep", .align = LV_ALIGN_BOTTOM_LEFT, .offset = -25};
  lvx_sign_t save = {.title = "<", .text = "Save", .align = LV_ALIGN_BOTTOM_RIGHT, .offset = -50};
  lvx_sign_t off = {.title = ">", .text = "Off", .align = LV_ALIGN_BOTTOM_RIGHT, .offset = -25};
  lvx_sign_create(&start, lv_scr_act());
  lvx_sign_create(&white, lv_scr_act());
  lvx_sign_create(&light, lv_scr_act());
  lvx_sign_create(&deep, lv_scr_act());
  lvx_sign_create(&save, lv_scr_act());
  lvx_sign_create(&off, lv_scr_act());

  // end draw
  gfx_end();

  for (;;) {
    // get power
    pwr_state_t bat = pwr_get();

    // get date and time
    uint16_t year, month, day, hour, minute;
    sys_get_date(&year, &month, &day);
    sys_get_time(&hour, &minute);

    // prepare text
    const char* text =
        scr_fmt("%llds - %.0f%% - P%d - F%d\n%04d-%02d-%02d %02d:%02d\n%lu B", naos_millis() / 1000, bat.battery * 100,
                bat.usb, bat.fast, year, month, day, hour, minute, esp_get_free_heap_size());

    // update label
    gfx_begin(false, false);
    lv_label_set_text(label, text);
    gfx_end();

    // await event
    sig_event_t event = sig_await(SIG_SENSOR | SIG_KEYS, 0);

    // loop on sensor
    if (event == SIG_SENSOR) {
      continue;
    }

    // power off on right (with fallback)
    if (event == SIG_RIGHT) {
      scr_power_off();
      return scr_debug;
    }

    // handle up and down
    if (event == SIG_UP || event == SIG_DOWN) {
      // log sleep
      naos_log("sleeping... (deep=%d)", event == SIG_DOWN);

      // disable sensor
      sns_set(false);

      // sleep display
      epd_sleep();

      // set return
      scr_return = scr_debug;

      // perform sleep
      pwr_sleep(event == SIG_DOWN);

      // log wakeup
      naos_log("woke up!");

      // enable sensor
      sns_set(true);

      continue;
    }

    /* handle meta keys */

    // cleanup
    scr_cleanup(event == SIG_ESCAPE);

    // handle left
    if (event == SIG_LEFT) {
      scr_return = scr_debug;
      return scr_saver;
    }

    // handle enter
    if (event == SIG_ENTER) {
      return scr_test;
    }

    /* handle escape */

    return scr_menu;
  }
}

static void* scr_saver() {
  // prepare variables
  bool right = false;

  // begin draw
  gfx_begin(false, false);

  // add icons
  lv_obj_t* lock = lv_img_create(lv_scr_act());
  lv_img_set_src(lock, &img_lock);
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

  // end draw
  gfx_end();

  for (;;) {
    // get time
    uint16_t hour, minute;
    sys_get_time(&hour, &minute);

    // read sensor
    sns_state_t sensor = sns_get();

    // begin draw
    gfx_begin(false, false);

    // update values
    lv_label_set_text(time, scr_fmt("%02d:%02d", hour, minute));
    if (sensor.ok) {
      lv_label_set_text(co2, scr_fmt("%.0f ppm CO2", sensor.co2));
      lv_label_set_text(tmp, scr_fmt("%.1f °C", sensor.tmp));
      lv_label_set_text(hum, scr_fmt("%.1f%% RH", sensor.hum));
    }

    // align objects
    lv_align_t align = right ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT;
    lv_obj_align(lock, align, right ? -19 : 19, 19);
    lv_obj_align(time, align, right ? -19 : 19, 41);
    lv_obj_align(co2, align, right ? -19 : 19, 59);
    lv_obj_align(tmp, align, right ? -19 : 19, 77);
    lv_obj_align(hum, align, right ? -19 : 19, 95);
    if (record != NULL) {
      lv_obj_align(record, align, right ? -39 : 39, 20);
    }

    // end draw
    gfx_end();

    // wait some time
    sig_event_t event = sig_await(SIG_ENTER, 15000);

    // handle enter
    if (event == SIG_ENTER) {
      break;
    }

    // flip side
    right = !right;
  }

  // cleanup
  scr_cleanup(false);

  return scr_return;
}

static void* scr_exit() {
  // begin draw
  gfx_begin(false, true);

  // add signs
  lvx_sign_t stop = {.title = "A", .text = "Messung beenden", .align = LV_ALIGN_CENTER, .offset = -15};
  lvx_sign_t back = {.title = "B", .text = "Zurück zum Labor", .align = LV_ALIGN_CENTER, .offset = 15};
  lvx_sign_create(&stop, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end();

  // await event
  sig_event_t event = sig_await(SIG_META, SCR_ACTION_TIMEOUT);

  // cleanup
  scr_cleanup(false);

  // go back to view on timeout
  if (event == SIG_TIMEOUT) {
    return scr_view;
  }

  // set action
  if (scr_action == 0) {
    scr_action = STM_FROM_MEASUREMENT;
  }

  // handle enter
  if (event == SIG_ENTER) {
    // get file
    dat_file_t* file = rec_file();

    // stop recording
    rec_stop();

    // show message
    scr_message(scr_fmt("%s\n beendet!", file->title));

    // set action
    scr_action = STM_COMP_MEASUREMENT;
  }

  return scr_menu;
}

static void* scr_view() {
  // prepare variables
  static int8_t mode = 0;  // co2, tmp, hum
  static int32_t position = 0;
  static bool advanced = false;

  // clear position
  position = 0;

  // begin draw
  gfx_begin(false, false);

  // add bar
  lvx_bar_t bar = {0};
  lvx_bar_create(&bar, lv_scr_act());

  // TODO: Show current mark.

  // add chart
  lv_obj_t* chart = lv_canvas_create(lv_scr_act());
  static lv_color_t chart_buffer[LV_CANVAS_BUF_SIZE_TRUE_COLOR(288, 96)] = {0};
  lv_canvas_set_buffer(chart, chart_buffer, 288, 96, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(chart, LV_ALIGN_BOTTOM_LEFT, 5, -5);
  lv_canvas_fill_bg(chart, lv_color_white(), LV_OPA_COVER);

  // TODO: Add markers.

  // end draw
  gfx_end();

  for (;;) {
    // get time
    uint16_t hour, minute;
    sys_get_time(&hour, &minute);

    // read sensor
    sns_state_t sensor = sns_get();

    // adjust position to last 5m or less if this is the recording file
    if (rec_running() && rec_file() == scr_file) {
      position = (int32_t)(sys_get_timestamp() - scr_file->head.start - (5 * 60 * 1000));
      if (position < 0) {
        position = 0;
      }
    }

    // calculate resolution and range
    int32_t resolution = 5000;
    int32_t start = position;
    int32_t end = position + 72 * resolution;

    // query points
    if (scr_file->size > 0) {
      dat_query(scr_file->head.num, scr_points, SCR_CHART_POINTS, position, resolution);
    }

    // begin draw
    gfx_begin(false, advanced);

    // update bar
    bar.time = scr_fmt("%02d:%02d", hour, minute);
    if (mode == 0) {
      bar.value = scr_fmt("%.0f ppm CO2", sensor.co2);
    } else if (mode == 1) {
      bar.value = scr_fmt("%.1f °C", sensor.tmp);
    } else if (mode == 2) {
      bar.value = scr_fmt("%.1f%% RH", sensor.hum);
    }
    lvx_bar_update(&bar);

    // draw chart bars
    lv_canvas_fill_bg(chart, lv_color_white(), LV_OPA_COVER);
    float range = mode == 0 ? 3000 : 100;
    lv_draw_line_dsc_t bar_desc = {.color = lv_color_black(), .width = 2, .opa = LV_OPA_COVER};
    for (size_t i = 0; i < SCR_CHART_POINTS; i++) {
      float value = mode == 0 ? scr_points[i].co2 : mode == 1 ? scr_points[i].tmp : scr_points[i].hum;
      lv_coord_t h = 2 + a32_safe_map_f(value, 0, range, 0, 78);
      lv_point_t points[2] = {{.x = 1 + i * 4, .y = 80}, {.x = 1 + i * 4, .y = 80 - h}};
      lv_canvas_draw_line(chart, points, 2, &bar_desc);
    }

    // draw chart labels
    lv_draw_label_dsc_t lbl_desc = {
        .font = &fnt_small, .color = lv_color_black(), .opa = LV_OPA_COVER, .align = LV_TEXT_ALIGN_LEFT};
    for (size_t i = 0; i < 3; i++) {
      // labels are position on the nearest minute mark using the following grid
      // < 1/6 |   1/3   |   1/3   |   1/3   | 1/6 >

      // get minuted aligned position
      float step = (float)(end - start) / 6.f;
      float pos = (float)start + step + (float)(i) * (step * 2);
      pos = roundf(pos / 60000) * 60000;

      // TODO: Show absolute timestamp.

      // format label
      const char* str = scr_ms2ts((int32_t)pos);

      // calculate coordinate
      lv_coord_t x = (lv_coord_t)a32_map_f(pos, (float)start, (float)end, 0, 288);
      x -= lv_txt_get_width(str, strlen(str), &fnt_small, 0, 0) / 2;

      // draw label
      lv_canvas_draw_text(chart, x, 88, 99, &lbl_desc, str);
    }

    // end draw
    gfx_end();

    // await event
    sig_event_t filter = SIG_KEYS;
    if (rec_running() && rec_file() == scr_file) {
      filter |= SIG_APPEND;
    }
    sig_event_t event = sig_await(filter, SCR_IDLE_TIMEOUT);

    // handle idle timeout
    if (event == SIG_TIMEOUT) {
      // cleanup
      scr_cleanup(false);

      // set return
      scr_return = scr_view;

      return scr_saver;
    }

    // handle escape
    if (event == SIG_ESCAPE) {
      // handle advanced
      if (advanced) {
        advanced = false;
        continue;
      }

      // cleanup
      scr_cleanup(false);

      // handle recording
      if (rec_running() && rec_file() == scr_file) {
        return scr_exit;
      }

      // set action
      scr_action = STM_FROM_ANALYSIS;

      return scr_edit;
    }

    // add mark on enter
    if (event == SIG_ENTER) {
      if (rec_running() && rec_file() == scr_file) {
        rec_mark();
      } else {
        advanced = true;
      }
      continue;
    }

    // update on append
    if (event == SIG_APPEND) {
      continue;
    }

    // change mode on up/down
    if (event == SIG_UP) {
      mode++;
      if (mode > 2) {
        mode = 0;
      }
      continue;
    } else if (event == SIG_DOWN) {
      mode--;
      if (mode < 0) {
        mode = 2;
      }
      continue;
    }

    // change position on left/right
    if (event == SIG_LEFT) {
      position -= SCR_POSITION_STEP;
    } else if (event == SIG_RIGHT) {
      position += SCR_POSITION_STEP;
    }
    if (position > scr_file->stop - SCR_POSITION_STEP) {
      position = scr_file->stop - SCR_POSITION_STEP;
    }
    if (position < 0) {
      position = 0;
    }
  }
}

static void* scr_create() {
  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, "Neue Messung erstellen?");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);

  // add name
  lv_obj_t* name = lv_label_create(lv_scr_act());
  lv_label_set_text(name, scr_fmt("Messung %u", dat_next()));
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 5, 26);

  // add mode
  lv_obj_t* mode = lv_label_create(lv_scr_act());
  lv_label_set_text(mode, "CO2, TEMP, RH");
  lv_obj_align(mode, LV_ALIGN_TOP_LEFT, 5, 47);

  // add signs
  lvx_sign_t start = {.title = "A", .text = "Start", .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_t back = {.title = "B", .text = "Zurück", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_create(&start, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end();

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_META, SCR_ACTION_TIMEOUT);

    // cleanup
    scr_cleanup(false);

    // handle escape and timeout
    if (event == SIG_ESCAPE || event == SIG_TIMEOUT) {
      return scr_menu;
    }

    /* handle enter */

    // create measurement
    scr_file = dat_create(sys_get_timestamp());

    // start recording
    rec_start(scr_file);

    // set action
    if (scr_file->head.num == 1) {
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

  // add text
  lv_obj_t* text = lv_label_create(lv_scr_act());
  lv_label_set_text(text, scr_fmt("%s\nwirklich löschen?", scr_file->title));
  lv_obj_align(text, LV_ALIGN_TOP_MID, 0, 25);
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  // add signs
  lvx_sign_t next = {.title = "A", .text = "Löschen", .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_t back = {.title = "B", .text = "Zurück", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_create(&next, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end();

  // await event
  sig_event_t event = sig_await(SIG_META, SCR_ACTION_TIMEOUT);

  // cleanup
  scr_cleanup(false);

  // handle escape and timeout
  if (event == SIG_ESCAPE || event == SIG_TIMEOUT) {
    return scr_edit;
  }

  /* handle enter */

  // capture num
  uint16_t num = scr_file->head.num;

  // delete file
  dat_delete(scr_file->head.num);

  // show message
  scr_message(scr_fmt("Messung %d\nerfolgreich gelöscht!", num));

  return scr_explore;
}

static void* scr_edit() {
  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, scr_file->title);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);

  // add date
  lv_obj_t* date = lv_label_create(lv_scr_act());
  lv_label_set_text(date, scr_file->date);
  lv_obj_align(date, LV_ALIGN_TOP_LEFT, 5, 26);

  // add length
  lv_obj_t* length = lv_label_create(lv_scr_act());
  lv_label_set_text(length, scr_ms2str(scr_file->stop));
  lv_obj_align(length, LV_ALIGN_TOP_MID, 0, 26);

  // add signs
  lvx_sign_t analyze = {.title = "A", .text = "Analyse", .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_t back = {.title = "B", .text = "Zurück", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_t delete = {.title = "<", .text = "Löschen", .align = LV_ALIGN_BOTTOM_LEFT, .offset = -25};
  lvx_sign_create(&analyze, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&delete, lv_scr_act());

  // end draw
  gfx_end();

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_META | SIG_LEFT, SCR_ACTION_TIMEOUT);

    // cleanup
    scr_cleanup(false);

    // handle event
    switch (event) {
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
    scr_message("Keine gespeicherte\nMessungen...");

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
    lv_label_set_text(names[i], "");
    lv_label_set_text(dates[i], "");
    lv_obj_set_size(rects[i], lv_pct(100), 25);
    lv_obj_align(rects[i], LV_ALIGN_TOP_LEFT, 0, 0 + i * 25);
    lv_obj_align(names[i], LV_ALIGN_TOP_LEFT, 5, 5 + i * 25);
    lv_obj_align(dates[i], LV_ALIGN_TOP_RIGHT, -(5 - FNT_OFF), 5 + i * 25);
    lv_obj_set_style_border_width(rects[i], 0, LV_PART_MAIN);
    lv_obj_set_style_radius(rects[i], 0, LV_PART_MAIN);
  }

  // add signs
  lvx_sign_t back = {.title = "B", .text = "Zurück", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_t open = {.title = "A", .text = "Öffnen", .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_create(&open, lv_scr_act());

  // add info
  lv_obj_t* info = lv_label_create(lv_scr_act());
  lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -5);

  // end draw
  gfx_end();

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
      dat_file_t* file = dat_file_list() + index;

      // update labels
      lv_label_set_text(names[i], file->title);
      lv_label_set_text(dates[i], file->date);

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
    gfx_end();

    // await event
    sig_event_t event = sig_await(SIG_VERT | SIG_META, SCR_ACTION_TIMEOUT);

    // handle arrows
    if (event == SIG_UP) {
      if (selected > 0) {
        selected--;
      }
      if (offset > selected) {
        offset = selected;
      }
      continue;
    }
    if (event == SIG_DOWN) {
      if (selected < total - 1) {
        selected++;
      }
      if (selected > offset + 3) {
        offset = selected - 3;
      }
      continue;
    }

    /* handle meta and timeout */

    // cleanup
    scr_cleanup(false);

    // handle escape and timeout
    if (event == SIG_ESCAPE || event == SIG_TIMEOUT) {
      return scr_menu;
    }

    /* handle enter */

    // set file
    scr_file = dat_file_list() + selected;

    return scr_edit;
  }
}

static void* scr_reset() {
  // begin draw
  gfx_begin(false, true);

  // add text
  lv_obj_t* text = lv_label_create(lv_scr_act());
  lv_label_set_text(text, "Air Lab\nwirklich zurücksetzen?");
  lv_obj_align(text, LV_ALIGN_TOP_MID, 0, 25);
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  // add signs
  lvx_sign_t next = {.title = "A", .text = "Ja", .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_t back = {.title = "B", .text = "Nein", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_create(&next, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end();

  // await event
  sig_event_t event = sig_await(SIG_META, SCR_ACTION_TIMEOUT);

  // cleanup
  scr_cleanup(false);

  // handle escape
  if (event == SIG_ESCAPE || event == SIG_TIMEOUT) {
    return scr_settings;
  }

  /* handle enter */

  // reset data
  dat_reset();

  // show message
  scr_message("Air Lab\nerfolgreich zurückgesetzt!");

  return scr_intro;
}

static void* scr_settings() {
  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, "Einstellungen");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);

  // add signs
  lvx_sign_t back = {.title = "B", .text = "Zurück", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_t reset = {.title = "<", .text = "Reset", .align = LV_ALIGN_BOTTOM_LEFT, .offset = -25};
  lvx_sign_t datetime = {.title = "↑", .text = "Uhr + Datum", .align = LV_ALIGN_BOTTOM_LEFT, .offset = -50};
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&reset, lv_scr_act());
  lvx_sign_create(&datetime, lv_scr_act());

  // end draw
  gfx_end();

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_UP | SIG_LEFT | SIG_ESCAPE | SIG_ENTER, SCR_ACTION_TIMEOUT);

    // cleanup
    scr_cleanup(false);

    // handle event
    switch (event) {
      case SIG_UP:
        return scr_date;
      case SIG_LEFT:
        return scr_reset;
      case SIG_ESCAPE:
      case SIG_TIMEOUT:
        // set action
        scr_action = STM_FROM_SETTINGS;

        return scr_menu;
      case SIG_ENTER:
        return scr_debug;
      default:
        ESP_ERROR_CHECK(ESP_FAIL);
    }
  }
}

static void* scr_menu() {
  // prepare variables
  static int8_t mode = 0;  // co2, tmp, hum
  static int8_t opt = 0;   // settings, explore, create
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
  lv_img_set_src(robin, &img_robin);
  lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 10, -10);

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
  gfx_end();

  // prepare deadline
  int64_t deadline = naos_millis() + SCR_IDLE_TIMEOUT;

  // prepare flags
  bool exclaim = true;
  bool fun = false;

  // prepare statement
  stm_entry_t* statement = NULL;

  for (;;) {
    // get time
    uint16_t hour, minute;
    sys_get_time(&hour, &minute);

    // read sensor
    sns_state_t sensor = sns_get();

    // query sensor
    sns_hist_t hist = sns_query(mode == 0 ? SNS_CO2 : mode == 1 ? SNS_TMP : SNS_HUM);

    // query statement
    if (statement == NULL && (exclaim || fun)) {
      statement = stm_query(exclaim, scr_action);
    }

    // begin draw
    gfx_begin(false, false);

    // update bar
    bar.time = scr_fmt("%02d:%02d", hour, minute);
    if (!sensor.ok) {
      bar.value = "Keine Daten";
    } else if (mode == 0) {
      bar.value = scr_fmt("%.0f ppm CO2", sensor.co2);
    } else if (mode == 1) {
      bar.value = scr_fmt("%.1f °C", sensor.tmp);
    } else if (mode == 2) {
      bar.value = scr_fmt("%.1f%% RH", sensor.hum);
    }
    lvx_bar_update(&bar);

    // set icon
    if (opt == 0) {
      lv_img_set_src(icon, &img_cog);
    } else if (opt == 1) {
      lv_img_set_src(icon, &img_folder);
    } else if (opt == 2) {
      lv_img_set_src(icon, rec_running() ? &img_file2 : &img_file1);
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
    lv_point_t points[SNS_HIST] = {0};
    for (size_t i = 0; i < SNS_HIST; i++) {
      points[i].x = (lv_coord_t)a32_safe_map_i(i, 0, SNS_HIST - 1, 0, 24);
      points[i].y = (lv_coord_t)a32_safe_map_f(hist.values[i], hist.min, hist.max, 14, 2);
    }
    lv_draw_line_dsc_t line = {.color = lv_color_black(), .width = 2, .opa = LV_OPA_COVER};
    lv_canvas_draw_line(chart, points, SNS_HIST, &line);

    // draw drain
    lv_canvas_fill_bg(drain, lv_color_white(), LV_OPA_COVER);
    lv_coord_t drain_height = (lv_coord_t)a32_safe_map_f(hist.values[SNS_HIST - 1], hist.min, hist.max, 0, 9);
    lv_draw_rect_dsc_t rect = {.bg_opa = LV_OPA_COVER, .bg_color = lv_color_black()};
    lv_canvas_draw_rect(drain, 1, 1 + 9 - drain_height, 20, drain_height, &rect);

    // set bubble
    bubble.text = statement ? statement->text : NULL;
    lvx_bubble_update(&bubble);

    // end draw
    gfx_end();

    // clear flags
    exclaim = false;
    fun = false;

    // await event
    sig_event_t event = sig_await(SIG_SENSOR | SIG_ENTER | SIG_ARROWS, 0);

    // handle deadline
    if (event == SIG_SENSOR && naos_millis() > deadline) {
      event = SIG_TIMEOUT;
    } else if (event != SIG_SENSOR) {
      deadline = naos_millis() + SCR_IDLE_TIMEOUT;
    }

    // clear statement on any key
    if (statement != NULL && (event & SIG_KEYS) != 0) {
      statement = NULL;
      continue;
    }

    // loop on sensor
    if (event == SIG_SENSOR) {
      // show fun fact after half of deadline expired
      if (deadline - naos_millis() < SCR_IDLE_TIMEOUT / 2) {
        fun = true;
      }

      continue;
    }

    // change mode on up/down
    if (event == SIG_UP) {
      mode++;
      if (mode > 2) {
        mode = 0;
      }
      continue;
    } else if (event == SIG_DOWN) {
      mode--;
      if (mode < 0) {
        mode = 2;
      }
      continue;
    }

    // change opt left/right
    if (event == SIG_LEFT) {
      opt--;
      if (opt < 0) {
        opt = 2;
      }
      continue;
    } else if (event == SIG_RIGHT) {
      opt++;
      if (opt > 2) {
        opt = 0;
      }
      continue;
    }

    // cleanup
    scr_cleanup(false);

    // clear action
    scr_action = 0;

    // enter screen saver on timeout
    if (event == SIG_TIMEOUT) {
      // set return
      scr_return = scr_menu;

      return scr_saver;
    }

    // handle enter
    if (event == SIG_ENTER) {
      switch (opt) {
        case 0:  // settings
          return scr_settings;
        case 1:  // explore
          return scr_explore;
        case 2:  // create or view
          if (rec_running()) {
            scr_file = rec_file();
            return scr_view;
          } else {
            return scr_create;
          }
        default:
          ESP_ERROR_CHECK(ESP_FAIL);
      }
    }
  }
}

static void* scr_time() {
  // show message
  scr_message("Und was ist die\ngenaue Zeit gerade?");

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
  lvx_wheel_t minute = {.value = 30, .min = 1, .max = 59, .format = "%02d", .fixed = true};

  // assign current time if available
  if (sys_has_time()) {
    sys_get_time(&hour.value, &minute.value);
  }

  // add wheels
  lvx_wheel_create(&hour, row);
  lvx_wheel_create(&minute, row);

  // add button
  lvx_sign_t back = {.title = "B", .text = "Zurück", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_t next = {.title = "A", .text = "Weiter", .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&next, lv_scr_act());

  // end draw
  gfx_end();

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_KEYS, SCR_ACTION_TIMEOUT);

    // forward arrows
    if ((event & SIG_ARROWS) != 0) {
      lvx_handle(event, true);
      continue;
    }

    /* handle meta and timeout */

    // save time
    sys_set_time(hour.value, minute.value);

    // cleanup
    scr_cleanup(false);

    // handle escape event
    if (event == SIG_ESCAPE || event == SIG_TIMEOUT) {
      return scr_date;
    }

    // show message
    scr_message("Einstellungen\ngespeichert!");

    // section action
    scr_action = STM_FROM_INTRO;

    return scr_menu;
  }
}

static void* scr_date() {
  // show message
  scr_message("Welches Datum\nhaben wir heute?");

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

  // assign current date if available
  if (sys_has_date()) {
    sys_get_date(&year.value, &month.value, &day.value);
  }

  // add wheels
  lvx_wheel_create(&day, row);
  lvx_wheel_create(&month, row);
  lvx_wheel_create(&year, row);

  // add button
  lvx_sign_t next = {.title = "A", .text = "Weiter", .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_t off = {.title = "B", .text = "Abbrechen", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_create(&next, lv_scr_act());
  lvx_sign_create(&off, lv_scr_act());

  // end draw
  gfx_end();

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_META | SIG_ARROWS, SCR_ACTION_TIMEOUT);

    // handle arrows
    if ((event & SIG_ARROWS) != 0) {
      lvx_handle(event, true);
      continue;
    }

    // cleanup
    scr_cleanup(false);

    // power off on escape (with fallback)
    if (event == SIG_ESCAPE || event == SIG_TIMEOUT) {
      if (!sys_has_date() || !sys_has_time()) {
        scr_power_off();
        return scr_intro;
      } else {
        return scr_settings;
      }
    }

    /* handle enter */

    // save date
    sys_set_date(year.value, month.value, day.value);

    return scr_time;
  }
}

static void* scr_intro() {
  // wait a bit
  naos_delay(1000);

  // show robin
  gfx_begin(false, false);
  lv_obj_t* img = lv_img_create(lv_scr_act());
  lv_img_set_src(img, &img_robin);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
  gfx_end();

  // wait a bit
  naos_delay(2000);

  // show heart and title
  gfx_begin(false, false);
  lv_img_set_src(img, &img_heart);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, -15);
  lv_obj_t* lbl = lv_label_create(lv_scr_act());
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 15);
  lv_label_set_text(lbl, "Willkomen im Air Lab!");
  gfx_end();

  // wait a bit
  naos_delay(2000);

  // cleanup
  scr_cleanup(false);

  return scr_date;
}

/* Management */

void scr_task() {
  // prepare handler
  void* (*handler)() = scr_menu;

  // check settings
  if (!sys_has_date() || !sys_has_time()) {
    handler = scr_intro;
  }

  // handle return
  if (scr_return != NULL) {
    handler = scr_return;
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
