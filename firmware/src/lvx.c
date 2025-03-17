#include <naos.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <art32/numbers.h>

#include <al/power.h>

#include "lvx.h"
#include "gfx.h"
#include "fnt.h"
#include "img.h"
#include "rec.h"
#include "sys.h"
#include "gui.h"

static int lvx_num_digits(int n) {
  // calculate digits
  return n == 0 ? 1 : (int)(floor(log10(abs(n)))) + 1;
}

static lv_coord_t lvx_text_width(lv_obj_t* obj, const char* text) {
  const lv_font_t* font = lv_obj_get_style_text_font(obj, 0);
  lv_coord_t ls = lv_obj_get_style_text_letter_space(obj, 0);
  return lv_txt_get_width(text, strlen(text), font, ls, 0);
}

/* Formatting */

const char* lvx_fmt(const char* fmt, ...) {
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

/* Wheel */

static void lvx_wheel_refocus(lvx_wheel_t* wheel, bool focused) {
  // handle defocused
  if (!focused) {
    lv_obj_set_style_opa(wheel->_up, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_opa(wheel->_down, LV_OPA_TRANSP, LV_PART_MAIN);
    return;
  }

  // handle focused
  if (wheel->value > wheel->min) {
    lv_obj_set_style_opa(wheel->_down, LV_OPA_COVER, LV_PART_MAIN);
  } else {
    lv_obj_set_style_opa(wheel->_down, LV_OPA_TRANSP, LV_PART_MAIN);
  }
  if (wheel->value < wheel->max) {
    lv_obj_set_style_opa(wheel->_up, LV_OPA_COVER, LV_PART_MAIN);
  } else {
    lv_obj_set_style_opa(wheel->_up, LV_OPA_TRANSP, LV_PART_MAIN);
  }
}

static void lvx_wheel_key(lv_event_t* event) {
  // get value
  lvx_wheel_t* wheel = event->user_data;

  // handle key
  switch (lv_event_get_key(event)) {
    case LV_KEY_UP:
      if (wheel->value >= wheel->max) {
        wheel->value = wheel->min;
      } else {
        wheel->value++;
      }
      break;
    case LV_KEY_DOWN:
      if (wheel->value <= wheel->min) {
        wheel->value = wheel->max;
      } else {
        wheel->value--;
      }
      break;
    default:
      return;
  }

  // update text
  char text[32];
  snprintf(text, 32, wheel->format, wheel->value);
  lv_label_set_text(event->target, text);

  // update arrows
  lvx_wheel_refocus(wheel, true);
}

static void lvx_wheel_focus(lv_event_t* event) {
  // get wheel
  lvx_wheel_t* wheel = event->user_data;

  // handle event
  switch (event->code) {
    case LV_EVENT_FOCUSED:
      lvx_wheel_refocus(wheel, true);
      break;
    case LV_EVENT_DEFOCUSED:
      lvx_wheel_refocus(wheel, false);
      break;
    default:
      break;
  }
}

void lvx_wheel_create(lvx_wheel_t* wheel, lv_obj_t* parent) {
  // prepare static variables
  static lv_style_t base;
  static lv_style_t focused;
  static bool initialized = false;

  // initialize styles
  if (!initialized) {
    lv_style_init(&base);
    lv_style_set_bg_color(&base, lv_color_white());
    lv_style_set_bg_opa(&base, LV_OPA_COVER);
    lv_style_set_border_width(&base, 2);
    lv_style_set_border_color(&base, lv_color_black());
    lvx_style_set_pad(&base, 2, 2 - FNT_OFF, 5, 5 - FNT_OFF);
    lv_style_set_text_align(&base, LV_TEXT_ALIGN_CENTER);
    lv_style_init(&focused);
    lv_style_set_bg_color(&focused, lv_color_black());
    lv_style_set_text_color(&focused, lv_color_white());
    initialized = true;
  }

  // ensure format
  if (wheel->format == NULL) {
    wheel->format = "%d";
  }

  // add column
  wheel->_col = lv_obj_create(parent);
  lv_obj_set_size(wheel->_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(wheel->_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(wheel->_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(wheel->_col, 0, LV_PART_MAIN);

  // prepare text
  char text[32];
  snprintf(text, 32, wheel->format, wheel->value);

  // add up arrow
  wheel->_up = lv_img_create(wheel->_col);
  lv_img_set_src(wheel->_up, &img_arrow_up);

  // add label
  wheel->_lbl = lv_label_create(wheel->_col);
  lv_label_set_text(wheel->_lbl, text);
  lv_obj_add_style(wheel->_lbl, &base, LV_PART_MAIN);
  lv_obj_add_style(wheel->_lbl, &focused, LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_add_event_cb(wheel->_lbl, lvx_wheel_key, LV_EVENT_KEY, wheel);
  lv_group_add_obj(gfx_get_group(), wheel->_lbl);

  // handle size
  if (wheel->fixed) {
    snprintf(text, 32, "%0*d", lvx_num_digits(wheel->max), 0);
    lv_coord_t width = lvx_text_width(wheel->_lbl, text);
    width += lv_obj_get_style_pad_left(wheel->_lbl, 0);
    width += lv_obj_get_style_pad_right(wheel->_lbl, 0);
    width += lv_obj_get_style_border_width(wheel->_lbl, 0) * 2;
    lv_obj_set_width(wheel->_lbl, width);
  }

  // add down arrow
  wheel->_down = lv_img_create(wheel->_col);
  lv_img_set_src(wheel->_down, &img_arrow_down);

  // handle arrow focuses
  lv_obj_add_event_cb(wheel->_lbl, lvx_wheel_focus, LV_EVENT_ALL, wheel);
  lvx_wheel_refocus(wheel, lv_group_get_focused(gfx_get_group()) == wheel->_lbl);
}

/* Sign */

void lvx_sign_create(lvx_sign_t* sign, lv_obj_t* parent) {
  // prepare static variables
  static lv_style_t base;
  static lv_style_t title;
  static bool initialized = false;

  // initialize styles
  if (!initialized) {
    lv_style_init(&base);
    lv_style_set_bg_color(&base, lv_color_white());
    lv_style_set_bg_opa(&base, LV_OPA_COVER);
    lv_style_set_text_align(&base, LV_TEXT_ALIGN_CENTER);
    lvx_style_set_pad(&base, 2, 2 - FNT_OFF, 3, 3 - FNT_OFF);
    lv_style_init(&title);
    lv_style_set_bg_color(&title, lv_color_black());
    lv_style_set_text_color(&title, lv_color_white());
    initialized = true;
  }

  // add row
  sign->_row = lv_obj_create(parent);
  lv_obj_set_size(sign->_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_align(sign->_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_width(sign->_row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(sign->_row, 0, LV_PART_MAIN);

  // add title
  sign->_title = lv_label_create(sign->_row);
  lv_obj_set_size(sign->_title, 18, 18);
  lv_label_set_text(sign->_title, sign->title);
  lv_obj_add_style(sign->_title, &base, LV_PART_MAIN);
  lv_obj_add_style(sign->_title, &title, LV_PART_MAIN);

  // add text
  sign->_text = lv_label_create(sign->_row);
  lv_label_set_text(sign->_text, sign->text);
  lv_obj_add_style(sign->_text, &base, LV_PART_MAIN);

  // apply alignment
  lv_obj_set_align(sign->_row, sign->align);
  switch (sign->align) {
    case LV_ALIGN_BOTTOM_LEFT:
      lv_obj_set_pos(sign->_row, 5, -5 + sign->offset);
      lv_obj_set_flex_flow(sign->_row, LV_FLEX_FLOW_ROW);
      break;
    case LV_ALIGN_BOTTOM_RIGHT:
      lv_obj_set_pos(sign->_row, -5, -5 + sign->offset);
      lv_obj_set_flex_flow(sign->_row, LV_FLEX_FLOW_ROW_REVERSE);
      break;
    default:
      lv_obj_set_pos(sign->_row, 0, sign->offset);
      lv_obj_set_flex_flow(sign->_row, LV_FLEX_FLOW_ROW);
      break;
  }
}

/* Bar */

void lvx_bar_create(lvx_bar_t* bar, lv_obj_t* parent) {
  // add time
  bar->_time = lv_label_create(parent);
  lv_obj_align(bar->_time, LV_ALIGN_TOP_LEFT, 5, 5);

  // add power icon
  bar->_pwr = lv_img_create(parent);
  lv_obj_align(bar->_pwr, LV_ALIGN_TOP_LEFT, 60, 3);

  // add record icon if recording
  if (rec_running()) {
    bar->_rec = lv_img_create(parent);
    lv_img_set_src(bar->_rec, &img_record);
    lv_obj_align(bar->_rec, LV_ALIGN_TOP_LEFT, 80, 5);
  }

  // add mark label
  bar->_mrk = lv_label_create(parent);
  lv_obj_align(bar->_mrk, LV_ALIGN_TOP_LEFT, 100, 5);

  // add value
  bar->_val = lv_label_create(parent);
  lv_obj_align(bar->_val, LV_ALIGN_TOP_RIGHT, -(20 - FNT_OFF), 5);

  // add arrows
  bar->_ar1 = lv_img_create(parent);
  bar->_ar2 = lv_img_create(parent);
  lv_img_set_src(bar->_ar1, &img_arrow_up);
  lv_img_set_src(bar->_ar2, &img_arrow_down);
  lv_obj_align(bar->_ar1, LV_ALIGN_TOP_RIGHT, -5, 5);
  lv_obj_align(bar->_ar2, LV_ALIGN_TOP_RIGHT, -5, 13);
}

void lvx_bar_update(lvx_bar_t* bar) {
  // set time
  lv_label_set_text(bar->_time, bar->time);

  // read power
  al_power_state_t power = al_power_get();

  // update power
  if (power.usb && power.charging) {
    lv_img_set_src(bar->_pwr, &img_power);
  } else if (power.battery > 0.75) {
    lv_img_set_src(bar->_pwr, &img_bat3);
  } else if (power.battery > 0.5) {
    lv_img_set_src(bar->_pwr, &img_bat2);
  } else if (power.battery > 0.25) {
    lv_img_set_src(bar->_pwr, &img_bat1);
  } else {
    lv_img_set_src(bar->_pwr, &img_bat0);
  }

  // update mark
  lv_label_set_text(bar->_mrk, bar->mark != NULL ? bar->mark : "");

  // update value
  lv_label_set_text(bar->_val, bar->value);
}

/* Bubble */

void lvx_bubble_create(lvx_bubble_t* bubble, lv_obj_t* parent) {
  // create frame
  bubble->_frame = lv_img_create(parent);
  lv_obj_align(bubble->_frame, LV_ALIGN_BOTTOM_LEFT, 60, -30);
  lv_obj_add_flag(bubble->_frame, LV_OBJ_FLAG_HIDDEN);

  // create label
  bubble->_label = lv_label_create(parent);
  lv_obj_set_width(bubble->_label, 200);
  lv_obj_align(bubble->_label, LV_ALIGN_BOTTOM_LEFT, 76, -38);
  lv_label_set_text(bubble->_label, "");
  lv_obj_set_style_text_line_space(bubble->_label, 0, LV_PART_MAIN);
  lv_obj_add_flag(bubble->_label, LV_OBJ_FLAG_HIDDEN);
}

void lvx_bubble_update(lvx_bubble_t* bubble) {
  if (bubble->text != NULL) {
    // calculate height
    lv_point_t size = {0};
    lv_txt_get_size(&size, bubble->text, &fnt_16, 0, 0, 200, 0);

    // update frame
    lv_obj_clear_flag(bubble->_frame, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_src(bubble->_frame, size.y >= 48 ? &img_bubble3 : size.y >= 32 ? &img_bubble2 : &img_bubble1);

    // update label
    lv_obj_clear_flag(bubble->_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(bubble->_label, bubble->text);
  } else {
    // update frame
    lv_obj_add_flag(bubble->_frame, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_src(bubble->_frame, NULL);

    // update label
    lv_obj_add_flag(bubble->_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(bubble->_label, "");
  }
}

/* Canvas */

void lvx_chart_draw(lv_obj_t* canvas, lvx_chart_data_t data) {
  // draw chart bars and marks
  lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
  lv_draw_line_dsc_t bar_desc;
  lv_draw_line_dsc_init(&bar_desc);
  bar_desc.width = 2;
  for (size_t i = 0; i < LVX_CHART_SIZE; i++) {
    float value = data.values[i];
    lv_coord_t h = 2 + (lv_coord_t)a32_safe_map_f(value, 0, data.range, 0, 78);
    lv_point_t points[2] = {
        {.x = 1 + i * 4, .y = 80},
        {.x = 1 + i * 4, .y = 80 - h},
    };
    lv_canvas_draw_line(canvas, points, 2, &bar_desc);
    if (data.marks[i] > 0) {
      points[0].y = 82;
      points[1].y = 84;
      lv_canvas_draw_line(canvas, points, 2, &bar_desc);
    }
  }

  // draw chart arrows
  if (data.arrows) {
    lv_draw_img_dsc_t img_draw;
    lv_draw_img_dsc_init(&img_draw);
    if (data.start > 0) {
      lv_canvas_draw_img(canvas, 0, 96 - 7, &img_arrow_left, &img_draw);
    }
    if (data.end < data.stop) {
      lv_canvas_draw_img(canvas, 288 - 9, 96 - 7, &img_arrow_right, &img_draw);
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
    float step = (float)(data.end - data.start) / 6.f;
    float pos = (float)data.start + step + (float)(i) * (step * 2);
    pos = roundf(pos / 60000) * 60000;

    // format label
    uint16_t hour, minute;
    sys_conv_timestamp(data.offset + (int64_t)(pos), &hour, &minute, NULL);
    const char* str = lvx_fmt("%02d:%02d", hour, minute);

    // calculate coordinate
    lv_coord_t x = (lv_coord_t)a32_map_f(pos, (float)data.start, (float)data.end, 0, 288);
    x -= lv_txt_get_width(str, strlen(str), &fnt_8, 0, 0) / 2;

    // draw label
    lv_canvas_draw_text(canvas, x, 88, 99, &lbl_desc, str);
  }

  // draw cursor if requested
  if (data.cursor) {
    lv_point_t points[2] = {
        {.x = 1 + data.index * 4, .y = 87},
        {.x = 1 + data.index * 4, .y = 96},
    };
    lv_canvas_draw_line(canvas, points, 2, &bar_desc);
  }
}

/* Helpers */

static uint32_t lvx_key_map[] = {
    [SIG_ENTER] = LV_KEY_ENTER, [SIG_ESCAPE] = LV_KEY_ESC, [SIG_UP] = LV_KEY_UP,
    [SIG_DOWN] = LV_KEY_DOWN,   [SIG_LEFT] = LV_KEY_LEFT,  [SIG_RIGHT] = LV_KEY_RIGHT,
};

bool lvx_handle(sig_event_t event, bool focus) {
  // handle focus
  if (focus) {
    if (event.type == SIG_LEFT) {
      lv_group_focus_prev(gfx_get_group());
      return true;
    } else if (event.type == SIG_RIGHT) {
      lv_group_focus_next(gfx_get_group());
      return true;
    }
  }

  // handle scroll
  if (event.type == SIG_SCROLL) {
    int distance = (int)(event.touch * 2);
    if (distance < 0) {
      for (int i = 0; i < -distance; i++) {
        lv_group_send_data(gfx_get_group(), LV_KEY_DOWN);
      }
    } else if (distance > 0) {
      for (int i = 0; i < distance; i++) {
        lv_group_send_data(gfx_get_group(), LV_KEY_UP);
      }
    }
    return false;
  }

  // send event as key to group
  lv_group_send_data(gfx_get_group(), lvx_key_map[event.type]);

  return false;
}

void lvx_log_event(lv_event_t* event) {
  // prepare names
  static const char* names[] = {
      [LV_EVENT_PRESSED] = "PRESSED",
      [LV_EVENT_PRESSING] = "PRESSING",
      [LV_EVENT_PRESS_LOST] = "PRESS_LOST",
      [LV_EVENT_SHORT_CLICKED] = "SHORT_CLICKED",
      [LV_EVENT_LONG_PRESSED] = "LONG_PRESSED",
      [LV_EVENT_LONG_PRESSED_REPEAT] = "LONG_PRESSED_REPEAT",
      [LV_EVENT_CLICKED] = "CLICKED",
      [LV_EVENT_RELEASED] = "RELEASED",
      [LV_EVENT_SCROLL_BEGIN] = "SCROLL_BEGIN",
      [LV_EVENT_SCROLL_END] = "SCROLL_END",
      [LV_EVENT_SCROLL] = "SCROLL",
      [LV_EVENT_GESTURE] = "GESTURE",
      [LV_EVENT_KEY] = "KEY",
      [LV_EVENT_FOCUSED] = "FOCUSED",
      [LV_EVENT_DEFOCUSED] = "DEFOCUSED",
      [LV_EVENT_LEAVE] = "LEAVE",
      [LV_EVENT_HIT_TEST] = "HIT_TEST",
  };

  // check and log event
  if (event->code > 0 && event->code <= LV_EVENT_HIT_TEST) {
    naos_log("event: %s (%d)", names[event->code], event->code);
  }
}

void lvx_style_set_pad(lv_style_t* style, lv_coord_t top, lv_coord_t bottom, lv_coord_t left, lv_coord_t right) {
  lv_style_set_pad_top(style, top);
  lv_style_set_pad_bottom(style, bottom);
  lv_style_set_pad_left(style, left);
  lv_style_set_pad_right(style, right);
}
