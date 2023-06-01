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
#define SCR_MIN_RESOLUTION 5000

static stm_action_t scr_action = 0;
static dat_file_t* scr_file = NULL;
static dat_point_t scr_points[SCR_CHART_POINTS] = {0};
DEV_KEEP static void* scr_return_timeout = NULL;
DEV_KEEP static void* scr_return_unlock = NULL;

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
  lv_group_remove_all_objs(gfx_get_group());
  lv_obj_clean(lv_scr_act());
  gfx_end(false);
}

static void scr_message(const char* text, uint32_t timeout) {
  // show message
  gfx_begin(false, false);
  lv_obj_t* lbl = lv_label_create(lv_scr_act());
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_line_space(lbl, 6, LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  gfx_end(false);

  // wait some time
  naos_delay(timeout);

  // cleanup
  scr_cleanup(false);
}

static void scr_power_off() {
  // cleanup screen
  scr_cleanup(true);
  naos_delay(7500);

  // clear returns
  scr_return_timeout = NULL;
  scr_return_unlock = NULL;

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
  gfx_end(true);

  // prepare index
  int index = 0;

  for (;;) {
    // begin draw
    gfx_begin(false, false);

    // set bubble
    bubble.text = stm_get(index)->text;
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
  gfx_end(true);

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
    gfx_end(false);

    // await event
    sig_event_t event = sig_await(SIG_SENSOR | SIG_KEYS, 0);

    // loop on sensor
    if (event.type == SIG_SENSOR) {
      continue;
    }

    // power off on right
    if (event.type == SIG_RIGHT) {
      scr_power_off();
      continue;
    }

    // handle up and down
    if (event.type == SIG_UP || event.type == SIG_DOWN) {
      // log sleep
      naos_log("sleeping... (deep=%d)", event.type == SIG_DOWN);

      // disable sensor
      sns_set(false);

      // sleep display
      epd_sleep();

      // set return
      scr_return_unlock = scr_debug;

      // perform sleep
      pwr_sleep(event.type == SIG_DOWN, 0);

      // log wakeup
      naos_log("woke up!");

      // enable sensor
      sns_set(true);

      continue;
    }

    /* handle meta keys */

    // cleanup
    scr_cleanup(event.type == SIG_ESCAPE);

    // handle left
    if (event.type == SIG_LEFT) {
      // set return
      scr_return_unlock = scr_debug;

      return scr_saver;
    }

    // handle enter
    if (event.type == SIG_ENTER) {
      return scr_test;
    }

    /* handle escape */

    return scr_menu;
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
  lv_obj_t* lock = lv_img_create(lv_scr_act());
  lv_img_set_src(lock, &img_lock);
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

  // end draw
  gfx_end(true);

  for (;;) {
    // get time
    uint16_t hour, minute;
    sys_get_time(&hour, &minute);

    // read sensor
    sns_state_t sensor = sns_get();

    // await sensor if missing (deep sleep return)
    if (!sensor.ok) {
      sig_await(SIG_SENSOR, 0);
      sensor = sns_get();
    }

    // read power
    pwr_state_t power = pwr_get();

    // flip side
    right = !right;

    // begin draw
    gfx_begin(false, false);

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
    lv_obj_align(battery, align, right ? -39 : 39, 19);
    lv_obj_align(time, align, right ? -19 : 19, 41);
    lv_obj_align(co2, align, right ? -19 : 19, 59);
    lv_obj_align(tmp, align, right ? -19 : 19, 77);
    lv_obj_align(hum, align, right ? -19 : 19, 95);
    if (record != NULL) {
      lv_obj_align(record, align, right ? -55 : 55, 20);
    }

    // end draw
    gfx_end(false);

    // await draw
    naos_delay(1000);

    /* Sleep Control */

    // power off is battery is low and not charging
    if (power.battery < 0.10 && !power.usb && !power.charging) {
      naos_log("turing off due to low battery");  // TODO: Test!
      scr_power_off();
    }

    // deep sleep for 15s if not recording
    if (!rec_running()) {
      // TODO: Increase the longer we are in screen saver?
      pwr_sleep(true, 15000);
    }

    // otherwise, light sleep for 5s
    // TODO: Use adaptive sleeping for long measurements.
    pwr_cause_t cause = pwr_sleep(false, 5000);

    // handle unlock
    if (cause == PWR_UNLOCK) {
      break;
    }

    // await next measurement
    sig_await(SIG_APPEND, 0);
  }

  // cleanup
  scr_cleanup(false);

  return scr_return_unlock;
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
    dat_file_t* file = rec_file();

    // stop recording
    rec_stop();

    // show message
    scr_message(scr_fmt("%s\n beendet!", file->title), 2000);

    // set action
    scr_action = STM_COMP_MEASUREMENT;
  }

  return scr_menu;
}

static void* scr_view() {
  // prepare variables
  static int8_t mode = 0;  // co2, tmp, hum
  static bool advanced = false;

  // check recording
  bool recording = rec_running() && rec_file() == scr_file;

  // prepare position
  int32_t position = 0;
  if (!recording) {
    position = scr_file->stop / 2;
  }

  // zero points
  memset(scr_points, 0, sizeof(scr_points));

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

  for (;;) {
    // adjust position if recording
    if (recording) {
      position = scr_file->stop;
    }

    // calculate resolution
    int32_t resolution;
    if (recording) {
      resolution = SCR_MIN_RESOLUTION;
    } else if (advanced) {
      resolution = scr_file->stop / 10 / SCR_CHART_POINTS;
    } else {  // overview
      resolution = scr_file->stop / SCR_CHART_POINTS;
    }
    if (resolution < SCR_MIN_RESOLUTION) {
      resolution = SCR_MIN_RESOLUTION;
    }

    // calculate range
    int32_t start;
    int32_t end;
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
      if (end > scr_file->stop) {
        int32_t shift = fminf(start, end - scr_file->stop);
        end -= shift;
        start -= shift;
      }
    } else {  // overview
      start = 0;
      end = SCR_CHART_POINTS * resolution;
    }

    // calculate index
    size_t index = roundf(a32_safe_map_f(position, start, end, 0, SCR_CHART_POINTS - 1));

    // query points
    if (scr_file->size > 0) {
      size_t num = dat_query(scr_file->head.num, scr_points, SCR_CHART_POINTS, start, resolution);
      if (recording) {
        index = num - 1;
      }
    }

    // find marks
    uint8_t marks[SCR_CHART_POINTS] = {0};
    for (uint8_t i = 0; i < DAT_MARKS; i++) {
      if (scr_file->head.marks[i] > 0) {
        int32_t mark = roundf(a32_map_f(scr_file->head.marks[i], start, end, 0, SCR_CHART_POINTS - 1));
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
    sys_conv_timestamp(scr_file->head.start + (int64_t)current.offset, &hour, &minute, NULL);

    // begin draw
    gfx_begin(false, advanced);

    // update bar
    bar.time = scr_fmt("%02d:%02d", hour, minute);
    if (recording) {
      bar.mark = scr_file->marks > 0 ? scr_fmt("(M%d)", scr_file->marks) : "";
    } else {
      bar.mark = marks[index] > 0 ? scr_fmt("(M%d)", marks[index]) : "";
    }
    if (mode == 0) {
      bar.value = scr_fmt("%.0f ppm CO2", current.co2);
    } else if (mode == 1) {
      bar.value = scr_fmt("%.1f °C", current.tmp);
    } else if (mode == 2) {
      bar.value = scr_fmt("%.1f%% RH", current.hum);
    }
    lvx_bar_update(&bar);

    // draw chart bars and marks
    lv_canvas_fill_bg(chart, lv_color_white(), LV_OPA_COVER);
    float range = mode == 0 ? 3000 : 100;
    lv_draw_line_dsc_t bar_desc;
    lv_draw_line_dsc_init(&bar_desc);
    bar_desc.width = 2;
    for (size_t i = 0; i < SCR_CHART_POINTS; i++) {
      float value = mode == 0 ? scr_points[i].co2 : mode == 1 ? scr_points[i].tmp : scr_points[i].hum;
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
      if (end < scr_file->stop) {
        lv_canvas_draw_img(chart, 288 - 9, 96 - 7, &img_arrow_right, &img_draw);
      }
    }

    // draw chart labels
    lv_draw_label_dsc_t lbl_desc;
    lv_draw_label_dsc_init(&lbl_desc);
    lbl_desc.font = &fnt_small;
    lbl_desc.align = LV_TEXT_ALIGN_LEFT;
    for (size_t i = 0; i < 3; i++) {
      // labels are position on the nearest minute mark using the following grid
      // < 1/6 |   1/3   |   1/3   |   1/3   | 1/6 >

      // get minuted aligned position
      float step = (float)(end - start) / 6.f;
      float pos = (float)start + step + (float)(i) * (step * 2);
      pos = roundf(pos / 60000) * 60000;

      // format label
      sys_conv_timestamp(scr_file->head.start + (int64_t)(pos), &hour, &minute, NULL);
      const char* str = scr_fmt("%02d:%02d", hour, minute);

      // calculate coordinate
      lv_coord_t x = (lv_coord_t)a32_map_f(pos, (float)start, (float)end, 0, 288);
      x -= lv_txt_get_width(str, strlen(str), &fnt_small, 0, 0) / 2;

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
    sig_type_t filter = SIG_KEYS;
    if (recording) {
      filter |= SIG_APPEND;
    }
    sig_event_t event = sig_await(filter, SCR_IDLE_TIMEOUT);

    // TODO: Timeout does not work.

    // handle idle timeout
    if (event.type == SIG_TIMEOUT) {
      // cleanup
      scr_cleanup(false);

      // set return
      scr_return_unlock = scr_view;

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

    // update on append
    if (event.type == SIG_APPEND) {
      continue;
    }

    // change mode on up/down
    if (event.type == SIG_UP) {
      mode++;
      if (mode > 2) {
        mode = 0;
      }
      continue;
    } else if (event.type == SIG_DOWN) {
      mode--;
      if (mode < 0) {
        mode = 2;
      }
      continue;
    }

    // change position on left/right if not recording
    if (!recording) {
      if (event.type == SIG_LEFT) {
        position -= resolution * (event.repeat ? 5 : 1);
      } else if (event.type == SIG_RIGHT) {
        position += resolution * (event.repeat ? 5 : 1);
      }
      if (position > scr_file->stop) {
        position = scr_file->stop;
      }
      if (position < 0) {
        position = 0;
      }
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
  uint16_t num = scr_file->head.num;

  // delete file
  dat_delete(scr_file->head.num);

  // show message
  scr_message(scr_fmt("Messung %d\nerfolgreich gelöscht!", num), 2000);

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
    scr_message("Keine gespeicherte\nMessungen...", 2000);

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
  lvx_sign_t back = {.title = "B", .text = "Zurück", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_t open = {.title = "A", .text = "Öffnen", .align = LV_ALIGN_BOTTOM_RIGHT};
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
    gfx_end(false);

    // await event
    sig_event_t event = sig_await(SIG_VERT | SIG_META, SCR_ACTION_TIMEOUT);

    // handle arrows
    if (event.type == SIG_UP) {
      if (selected > 0) {
        selected--;
      }
      if (offset > selected) {
        offset = selected;
      }
      continue;
    }
    if (event.type == SIG_DOWN) {
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
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
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

  // reset date & time
  sys_reset();

  // show message
  scr_message("Air Lab\nerfolgreich zurückgesetzt!", 2000);

  return scr_intro;
}

static void* scr_settings() {
  // begin draw
  gfx_begin(false, false);

  // add title
  lv_obj_t* title = lv_label_create(lv_scr_act());
  lv_label_set_text(title, "Einstellungen");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);

  // add info
  lv_obj_t* info = lv_label_create(lv_scr_act());
  lv_label_set_text(info, "v" DEV_VERSION);
  lv_obj_align(info, LV_ALIGN_TOP_RIGHT, -5, 5);

  // add signs
  lvx_sign_t datetime = {.title = "↑", .text = "Uhr + Datum", .align = LV_ALIGN_BOTTOM_LEFT, .offset = -25};
  lvx_sign_t back = {.title = "B", .text = "Zurück", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_t off = {.title = ">", .text = "Ausschalten", .align = LV_ALIGN_BOTTOM_RIGHT, .offset = -25};
  lvx_sign_t reset = {.title = "<", .text = "Zurücksetzen", .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_create(&datetime, lv_scr_act());
  lvx_sign_create(&reset, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());
  lvx_sign_create(&off, lv_scr_act());

  // end draw
  gfx_end(false);

  for (;;) {
    // await event
    sig_type_t filter = SIG_UP | SIG_LEFT | SIG_RIGHT | SIG_ESCAPE;
#if DEV_MODE == 1
    filter |= SIG_ENTER;
#endif
    sig_event_t event = sig_await(filter, SCR_ACTION_TIMEOUT);

    // cleanup
    scr_cleanup(false);

    // handle event
    switch (event.type) {
      case SIG_UP:
        return scr_date;
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
  static int8_t opt = 0;   // create, explore, settings
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
      lv_img_set_src(icon, rec_running() ? &img_file2 : &img_file1);
    } else if (opt == 1) {
      lv_img_set_src(icon, &img_folder);
    } else if (opt == 2) {
      lv_img_set_src(icon, &img_cog);
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
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.width = 2;
    lv_canvas_draw_line(chart, points, SNS_HIST, &line_dsc);

    // draw drain
    lv_canvas_fill_bg(drain, lv_color_white(), LV_OPA_COVER);
    lv_coord_t drain_height = (lv_coord_t)a32_safe_map_f(hist.values[SNS_HIST - 1], hist.min, hist.max, 0, 9);
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_black();
    lv_canvas_draw_rect(drain, 1, 1 + 9 - drain_height, 20, drain_height, &rect_dsc);

    // set bubble
    bubble.text = statement ? statement->text : NULL;
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
          lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 19, -10);
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
      if (mode > 2) {
        mode = 0;
      }
      continue;
    } else if (event.type == SIG_DOWN) {
      mode--;
      if (mode < 0) {
        mode = 2;
      }
      continue;
    }

    // change opt left/right
    if (event.type == SIG_LEFT) {
      opt--;
      if (opt < 0) {
        opt = 2;
      }
      continue;
    } else if (event.type == SIG_RIGHT) {
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
    if (event.type == SIG_TIMEOUT) {
      // set return
      scr_return_unlock = scr_menu;

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
        default:
          ESP_ERROR_CHECK(ESP_FAIL);
      }
    }
  }
}

static void* scr_time() {
  // show message
  scr_message("Und wie spät ist es gerade?", 5000);

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
  gfx_end(false);

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_KEYS, SCR_ACTION_TIMEOUT);

    // forward arrows
    if ((event.type & SIG_ARROWS) != 0) {
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
    sys_set_time(hour.value, minute.value);

    // show message
    scr_message("Wie die Zeit vergeht...\nKomm, lass uns ins Labor gehen.", 5000);

    // section action
    scr_action = STM_FROM_INTRO;

    return scr_menu;
  }
}

static void* scr_date() {
  // show message
  scr_message("Ich habe zieeemlich\nlang geschlafen!\nWelcher Tag ist heute?", 5000);

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
  gfx_end(false);

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_META | SIG_ARROWS, SCR_ACTION_TIMEOUT);

    // handle arrows
    if ((event.type & SIG_ARROWS) != 0) {
      lvx_handle(event, true);
      continue;
    }

    // cleanup
    scr_cleanup(false);

    // power off or return on escape/timeout
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      if (!sys_has_date() || !sys_has_time()) {
        scr_power_off();
        continue;
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
  lv_label_set_text(lbl, "Hi! Ich bin Robin,\nProfessor für Luftwiss-\nenschaften im Air Lab.");
  gfx_end(false);

  // wait a bit
  naos_delay(3000);

  // cleanup
  scr_cleanup(false);

  return scr_date;
}

/* Management */

void scr_task() {
  // prepare handler
  void* (*handler)() = scr_menu;

  // check settings
#if DEV_MODE == 0
  if ((!sys_has_date() || !sys_has_time())) {
    handler = scr_intro;
  }
#endif

  // get wake up cause
  pwr_cause_t cause = pwr_cause();

  // handle return
  if (cause == PWR_UNLOCK && scr_return_unlock != NULL) {
    handler = scr_return_unlock;
  } else if (cause == PWR_TIMEOUT && scr_return_timeout != NULL) {
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
