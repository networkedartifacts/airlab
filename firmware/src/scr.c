#include <naos.h>
#include <naos_sys.h>
#include <lvgl.h>

#include "gfx.h"
#include "sig.h"
#include "sns.h"
#include "pwr.h"
#include "fnt.h"
#include "img.h"
#include "lvx.h"
#include "sys.h"
#include "dat.h"
#include "epd.h"

#define SCR_CHART_POINTS 72
#define SCR_POSITION_STEP 300000

static dat_file_t* scr_file = NULL;
static bool scr_record = false;
static dat_point_t scr_points[SCR_CHART_POINTS] = {0};
static const char* scr_message_text = NULL;
static void* scr_message_next = NULL;

/* Helpers */

static const char* scr_fmt(const char* fmt, ...) {
  static char str[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(str, sizeof(str), fmt, args);
  va_end(args);
  return str;
}

static void scr_cleanup(bool flush) {
  // clear group and screen
  gfx_begin(flush, false);
  lv_group_remove_all_objs(gfx_get_group());
  lv_obj_clean(lv_scr_act());
  gfx_end();
}

/* Screens */

static void* scr_edit();
static void* scr_explore();
static void* scr_menu();
static void* scr_settings();
static void* scr_date();
static void* scr_intro();

static void* scr_message() {
  // show heart and title
  gfx_begin(false, false);
  lv_obj_t* lbl = lv_label_create(lv_scr_act());
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(lbl, scr_message_text);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  gfx_end();

  // wait some time
  naos_delay(2000);

  // cleanup
  scr_cleanup(false);

  return scr_message_next;
}

static void* scr_debug() {
  // begin draw
  gfx_begin(false, false);

  // add label
  lv_obj_t* label = lv_label_create(lv_scr_act());
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(label, 12, LV_PART_MAIN);

  // add signs
  lvx_sign_t start = {.title = "A", .text = "Menu", .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_t white = {.title = "B", .text = "White", .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_t light = {.title = "↑", .text = "Light", .align = LV_ALIGN_BOTTOM_LEFT, .offset = -25};
  lvx_sign_t deep = {.title = "↓", .text = "Deep", .align = LV_ALIGN_BOTTOM_LEFT, .offset = -50};
  lvx_sign_t off = {.title = ">", .text = "Off", .align = LV_ALIGN_BOTTOM_RIGHT, .offset = -25};
  lvx_sign_create(&start, lv_scr_act());
  lvx_sign_create(&white, lv_scr_act());
  lvx_sign_create(&light, lv_scr_act());
  lvx_sign_create(&deep, lv_scr_act());
  lvx_sign_create(&off, lv_scr_act());

  // end draw
  gfx_end();

  for (;;) {
    // get states
    sns_state_t sns = sns_get();
    pwr_state_t bat = pwr_get();

    // get date and time
    uint16_t year, month, day, hour, minute;
    sys_get_date(&year, &month, &day);
    sys_get_time(&hour, &minute);

    // prepare text
    const char* text =
        scr_fmt("%d ppm - %.1f °C - %.0f%% rH\n%llds - %.0f%% - P%d - F%d\n %04d-%02d-%02d %02d:%02d", sns.co2, sns.tmp,
                sns.hum, naos_millis() / 1000, bat.battery * 100, bat.usb, bat.fast, year, month, day, hour, minute);

    // update label
    gfx_begin(false, false);
    lv_label_set_text(label, text);
    gfx_end();

    // await event
    sig_event_t event = sig_await(SIG_SENSOR | SIG_META | SIG_UP | SIG_DOWN | SIG_RIGHT, 5000);

    // loop on sensor
    if (event == SIG_SENSOR) {
      continue;
    }

    // handle off
    if (event == SIG_RIGHT) {
      // log
      naos_log("power off...");

      // power off
      pwr_off();

      continue;
    }

    // handle sleep
    if (event == SIG_UP || event == SIG_DOWN) {
      // log sleep
      naos_log("sleeping... (deep=%d)", event == SIG_DOWN);

      // disable sensor
      sns_set(false);

      // sleep display
      epd_sleep();

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

    // handle enter and escape
    if (event == SIG_ENTER) {
      return scr_menu;
    } else {  // SIG_ESCAPE
      naos_delay(5000);
      return scr_debug;
    }
  }
}

static void* scr_view() {
  // prepare variables
  static int8_t mode = 0;  // co2, tmp, hum
  static int32_t position = 0;

  // clear position
  position = 0;

  // begin draw
  gfx_begin(false, false);

  // add time
  lv_obj_t* time = lv_label_create(lv_scr_act());
  lv_obj_align(time, LV_ALIGN_TOP_LEFT, 5, 5);

  // TODO: Show measurement number.
  // TODO: Show record icon.

  // add info
  lv_obj_t* info = lv_label_create(lv_scr_act());
  lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 5);

  // add value
  lv_obj_t* value = lv_label_create(lv_scr_act());
  lv_obj_align(value, LV_ALIGN_TOP_RIGHT, -(5 - FNT_OFF), 5);

  // add chart
  lv_obj_t* chart = lv_chart_create(lv_scr_act());
  lv_obj_set_size(chart, lv_pct(100), 100);
  lv_obj_align(chart, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_pad_all(chart, 5, LV_PART_MAIN);
  lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
  lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
  lv_chart_set_point_count(chart, SCR_CHART_POINTS);
  lv_chart_set_div_line_count(chart, 0, 0);
  lv_obj_set_style_pad_column(chart, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_column(chart, 0, LV_PART_ITEMS);
  lv_obj_set_style_radius(chart, 0, LV_PART_ITEMS);

  // add series
  lv_chart_series_t* series = lv_chart_add_series(chart, lv_color_black(), LV_CHART_AXIS_PRIMARY_Y);

  // TODO: Add ticks and labels.
  // TODO: Add markers.

  // end draw
  gfx_end();

  for (;;) {
    // get time
    uint16_t hour, minute;
    sys_get_time(&hour, &minute);

    // read sensor
    sns_state_t sensor = sns_get();

    // adjust position to last 5m or less if recording
    if (scr_record) {
      position = (int32_t)(sys_get_timestamp() - scr_file->head.start - (5 * 60 * 1000));
      if (position < 0) {
        position = 0;
      }
    }

    // query points with 5s resolution
    if (scr_file->size > 0) {
      dat_query(scr_file->head.num, scr_points, SCR_CHART_POINTS, position, 5000);
    }

    // begin draw
    gfx_begin(false, false);

    // set time
    lv_label_set_text(time, scr_fmt("%02d:%02d", hour, minute));

    // set info and value
    if (mode == 0) {
      lv_label_set_text(info, "CO2");
      lv_label_set_text(value, scr_fmt("%d ppm", sensor.co2));
    } else if (mode == 1) {
      lv_label_set_text(info, "TMP");
      lv_label_set_text(value, scr_fmt("%.1f °C", sensor.tmp));
    } else if (mode == 2) {
      lv_label_set_text(info, "HUM");
      lv_label_set_text(value, scr_fmt("%.0f%% rH", sensor.hum));
    }

    // update plot
    lv_chart_set_all_value(chart, series, 0);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, mode == 0 ? 3000 : 100);
    for (int i = 0; i < scr_file->size; i++) {
      if (mode == 0) {
        lv_chart_set_value_by_id(chart, series, i, (lv_coord_t)scr_points[i].co2);
      } else if (mode == 1) {
        lv_chart_set_value_by_id(chart, series, i, (lv_coord_t)scr_points[i].tmp);
      } else if (mode == 2) {
        lv_chart_set_value_by_id(chart, series, i, (lv_coord_t)scr_points[i].hum);
      }
    }

    // end draw
    gfx_end();

    // await event
    sig_event_t event = sig_await(SIG_SENSOR | SIG_ARROWS | SIG_ESCAPE, 0);

    // handle escape
    if (event == SIG_ESCAPE) {
      // cleanup
      scr_cleanup(false);

      return scr_edit;
    }

    // loop on sensor
    if (event == SIG_SENSOR) {
      // skip if not recording
      if (!scr_record) {
        continue;
      }

      // get state
      sns_state_t state = sns_get();

      // calculate offset
      int64_t offset = sys_get_timestamp() - scr_file->head.start;

      // prepare point
      dat_point_t point = {
          .offset = (int32_t)offset,
          .co2 = state.co2,
          .hum = state.hum,
          .tmp = state.tmp,
      };

      // append point
      dat_append(scr_file->head.num, &point, 1);

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
      if (position < 0) {
        position = 0;
      }
    } else if (event == SIG_RIGHT) {
      position += SCR_POSITION_STEP;
      if (position > scr_file->stop - SCR_POSITION_STEP) {
        position = scr_file->stop - SCR_POSITION_STEP;
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
  gfx_end();

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_META, 0);

    // cleanup
    scr_cleanup(false);

    // handle escape
    if (event == SIG_ESCAPE) {
      return scr_menu;
    }

    /* handle enter */

    // create measurement
    scr_file = dat_create(sys_get_timestamp());

    // set record mode
    scr_record = true;

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
  sig_event_t event = sig_await(SIG_META, 0);

  // cleanup
  scr_cleanup(false);

  // handle escape
  if (event == SIG_ESCAPE) {
    return scr_edit;
  }

  /* handle enter */

  // delete file
  dat_delete(scr_file->head.num);

  // configure message
  scr_message_text = "Messung\nerfolgreich gelöscht!";
  scr_message_next = scr_explore;

  return scr_message;
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
    sig_event_t event = sig_await(SIG_META | SIG_LEFT, 0);

    // cleanup
    scr_cleanup(false);

    // handle event
    switch (event) {
      case SIG_ESCAPE:
        return scr_explore;
      case SIG_LEFT:
        return scr_delete;
      case SIG_ENTER:
        // disable record
        scr_record = false;

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
  if (total > 0) {
    lvx_sign_t open = {.title = "A", .text = "Öffnen", .align = LV_ALIGN_BOTTOM_RIGHT};
    lvx_sign_create(&open, lv_scr_act());
  }

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

    // end draw
    gfx_end();

    // await event
    sig_event_t event = sig_await(SIG_VERT | SIG_META, 0);

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

    /* handle meta keys */

    // ignore enter if there are no measurements
    if (total == 0 && event == SIG_ENTER) {
      continue;
    }

    // cleanup
    scr_cleanup(false);

    // handle escape
    if (event == SIG_ESCAPE) {
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
  sig_event_t event = sig_await(SIG_META, 0);

  // cleanup
  scr_cleanup(false);

  // handle escape
  if (event == SIG_ESCAPE) {
    return scr_settings;
  }

  /* handle enter */

  // reset data
  dat_reset();

  // configure message
  scr_message_text = "Air Lab\nerfolgreich zurückgesetzt!";
  scr_message_next = scr_intro;

  return scr_message;
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
    sig_event_t event = sig_await(SIG_UP | SIG_LEFT | SIG_ESCAPE | SIG_ENTER, 0);

    // cleanup
    scr_cleanup(false);

    // handle event
    switch (event) {
      case SIG_UP:
        return scr_date;
      case SIG_LEFT:
        return scr_reset;
      case SIG_ESCAPE:
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

  // add time
  lv_obj_t* time = lv_label_create(lv_scr_act());
  lv_obj_align(time, LV_ALIGN_TOP_LEFT, 5, 5);

  // add power
  lv_obj_t* pwr = lv_img_create(lv_scr_act());
  lv_obj_align(pwr, LV_ALIGN_TOP_LEFT, 50, 4);

  // add value
  lv_obj_t* value = lv_label_create(lv_scr_act());
  lv_obj_align(value, LV_ALIGN_TOP_RIGHT, -(20 - FNT_OFF), 5);

  // add arrow
  lv_obj_t* arrow1 = lv_img_create(lv_scr_act());
  lv_obj_t* arrow2 = lv_img_create(lv_scr_act());
  lv_img_set_src(arrow1, &img_arrow_up);
  lv_img_set_src(arrow2, &img_arrow_down);
  lv_obj_align(arrow1, LV_ALIGN_TOP_RIGHT, -5, 5);
  lv_obj_align(arrow2, LV_ALIGN_TOP_RIGHT, -5, 13);

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
  lv_obj_align(fan, LV_ALIGN_BOTTOM_RIGHT, -23, -39);

  // end draw
  gfx_end();

  for (;;) {
    // get time
    uint16_t hour, minute;
    sys_get_time(&hour, &minute);

    // read power
    pwr_state_t power = pwr_get();

    // read sensor
    sns_state_t sensor = sns_get();

    // begin draw
    gfx_begin(false, false);

    // set time
    lv_label_set_text(time, scr_fmt("%02d:%02d", hour, minute));

    // set power
    if (power.usb) {
      // TODO: Set powered icon.
      lv_img_set_src(pwr, &img_bat3);
    } else if (power.battery > 0.75) {
      lv_img_set_src(pwr, &img_bat3);
    } else if (power.battery > 0.5) {
      lv_img_set_src(pwr, &img_bat2);
    } else if (power.battery > 0.25) {
      lv_img_set_src(pwr, &img_bat1);
    } else {
      lv_img_set_src(pwr, &img_bat0);
    }

    // set info and value
    if (mode == 0) {
      lv_label_set_text(value, scr_fmt("%d ppm CO2", sensor.co2));
    } else if (mode == 1) {
      lv_label_set_text(value, scr_fmt("%.1f °C", sensor.tmp));
    } else if (mode == 2) {
      lv_label_set_text(value, scr_fmt("%.1f%% rH", sensor.hum));
    }

    // set icon
    if (opt == 0) {
      lv_img_set_src(icon, &img_cog);
    } else if (opt == 1) {
      lv_img_set_src(icon, &img_folder);
    } else if (opt == 2) {
      lv_img_set_src(icon, &img_file1);
    }

    // set fan
    fan_alt = !fan_alt;
    if (fan_alt) {
      lv_img_set_src(fan, &img_fan2);
    } else {
      lv_img_set_src(fan, &img_fan1);
    }

    // end draw
    gfx_end();

    // await event
    sig_event_t event = sig_await(SIG_SENSOR | SIG_ENTER | SIG_ARROWS, 0);

    // loop on sensor
    if (event == SIG_SENSOR) {
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

    // handle enter
    if (event == SIG_ENTER) {
      switch (opt) {
        case 0:  // settings
          return scr_settings;
        case 1:  // explore
          return scr_explore;
        case 2:  // create
          return scr_create;
        default:
          ESP_ERROR_CHECK(ESP_FAIL);
      }
    }
  }
}

static void* scr_time() {
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
    sig_event_t event = sig_await(SIG_KEYS, 0);

    // forward arrows
    if ((event & SIG_ARROWS) != 0) {
      lvx_handle(event, true);
      continue;
    }

    /* handle meta keys */

    // save time
    sys_set_time(hour.value, minute.value);

    // cleanup
    scr_cleanup(false);

    // handle escape event
    if (event == SIG_ESCAPE) {
      return scr_date;
    }

    // configure message
    scr_message_text = "Einstellungen\ngespeichert!";
    scr_message_next = scr_menu;

    return scr_message;
  }
}

static void* scr_date() {
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
  lvx_sign_t button = {.title = "A", .text = "Weiter", .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_create(&button, lv_scr_act());

  // end draw
  gfx_end();

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_ENTER | SIG_ARROWS, 0);

    // handle arrows
    if ((event & SIG_ARROWS) != 0) {
      lvx_handle(event, true);
      continue;
    }

    /* handle enter */

    // save date
    sys_set_date(year.value, month.value, day.value);

    // cleanup
    scr_cleanup(false);

    return scr_time;
  }
}

static void* scr_intro() {
  // wait a bit
  naos_delay(1000);

  // show heart and title
  gfx_begin(false, false);
  lv_obj_t* img = lv_img_create(lv_scr_act());
  lv_img_set_src(img, &img_heart);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, -15);
  lv_obj_t* lbl = lv_label_create(lv_scr_act());
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 15);
  lv_label_set_text(lbl, "Willkomen im Air Lab!");
  gfx_end();

  // wait a bit
  naos_delay(2000);

  // clear screen and show robin
  gfx_begin(false, false);
  lv_obj_clean(lv_scr_act());
  img = lv_img_create(lv_scr_act());
  lv_img_set_src(img, &img_robin);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
  gfx_end();

  // await a bit
  naos_delay(2000);

  // cleanup
  scr_cleanup(false);

  return scr_date;
}

/* Management */

void scr_task() {
  // prepare handler
  void* (*handler)() = scr_menu;

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
