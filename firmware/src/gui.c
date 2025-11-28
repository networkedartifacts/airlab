#include <lvgl.h>
#include <stdio.h>

#include "gui.h"

#include <naos.h>
#include <naos/sys.h>

#include "gfx.h"
#include "lvx.h"
#include "sig.h"
#include "fnt.h"

void gui_cleanup(bool refresh) {
  // clear group and screen
  gfx_begin(refresh, false);
  lv_disp_set_rotation(NULL, LV_DISP_ROT_NONE);
  lv_obj_clean(lv_scr_act());
  gfx_end(false, refresh);
}

void gui_write(const char* text, bool wait) {
  // show message
  gfx_begin(false, false);
  lv_obj_t* lbl = lv_label_create(lv_scr_act());
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_line_space(lbl, 6, LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  gfx_end(false, wait);
}

static lv_obj_t* gui_progress_bar = NULL;
static int64_t gui_progress_updated = 0;

void gui_progress_start(const char* text) {
  // cleanup screen
  gui_cleanup(false);

  // begin draw
  gfx_begin(false, false);

  // add label
  lv_obj_t* lbl = lv_label_create(lv_scr_act());
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -20);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_line_space(lbl, 6, LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  // add bar
  gui_progress_bar = lv_bar_create(lv_scr_act());
  lv_obj_set_size(gui_progress_bar, 200, 10);
  lv_obj_align(gui_progress_bar, LV_ALIGN_CENTER, 0, 20);
  lv_obj_set_style_radius(gui_progress_bar, 0, LV_PART_MAIN);
  lv_bar_set_value(gui_progress_bar, 0, LV_ANIM_OFF);

  // end draw
  gfx_end(false, false);

  // clear position
  gui_progress_updated = 0;
}

void gui_progress_update(size_t current, size_t total) {
  // check if too early
  if (naos_millis() - gui_progress_updated < 500) {
    return;
  }

  // update bar
  gfx_begin(false, false);
  lv_bar_set_value(gui_progress_bar, (int)((current * 100) / total), LV_ANIM_OFF);
  gfx_end(false, false);

  // update position
  gui_progress_updated = naos_millis();
}

void gui_message(const char* text, uint32_t timeout) {
  // show message
  gui_write(text, false);

  // wait some time
  sig_await(SIG_KEYS | SIG_TIMEOUT, timeout);

  // cleanup
  gui_cleanup(false);
}

bool gui_confirm(const char* message, const char* confirm, const char* cancel, bool invert, int64_t timeout) {
  // begin draw
  gfx_begin(false, invert);

  // add text
  lv_obj_t* text = lv_label_create(lv_scr_act());
  lv_label_set_text(text, message);
  lv_obj_align(text, LV_ALIGN_TOP_MID, 0, 25);
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(text, 6, LV_PART_MAIN);

  // add signs
  lvx_sign_t next = {
      .title = "A",
      .text = confirm,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t back = {
      .title = "B",
      .text = cancel,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&next, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // await event
  sig_event_t event = sig_await(SIG_META, timeout);

  // cleanup
  gui_cleanup(false);

  // handle escape and timeout
  if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
    return false;
  }

  return true;
}

int gui_choose(const char* first, const char* second, bool invert, int64_t timeout) {
  // begin draw
  gfx_begin(false, invert);

  // add signs
  lvx_sign_t stop = {
      .title = "A",
      .text = first,
      .align = LV_ALIGN_CENTER,
      .offset = -15,
  };
  lvx_sign_t back = {
      .title = "B",
      .text = second,
      .align = LV_ALIGN_CENTER,
      .offset = 15,
  };
  lvx_sign_create(&stop, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // await event
  sig_event_t event = sig_await(SIG_META, timeout);

  // cleanup
  gui_cleanup(false);

  // return zero on timeout
  if (event.type == SIG_TIMEOUT) {
    return 0;
  }

  // return one on enter
  if (event.type == SIG_ENTER) {
    return 1;
  }

  return 2;
}

int gui_list(int total, int selected, int* offset, const char* select, const char* cancel, gui_list_cb_t cb, void* ctx,
             int64_t timeout) {
  // handle empty
  if (total <= 0) {
    return -1;
  }

  // limit start
  if (selected < 0) {
    selected = 0;
  } else if (selected >= total) {
    selected = total - 1;
  }

  // adjust offset
  if (selected > *offset + 3) {
    *offset = selected - 3;
  } else if (selected < *offset) {
    *offset = selected;
  }

  // begin draw
  gfx_begin(false, false);

  // add list
  lv_obj_t* rects[4];
  lv_obj_t* names[4];
  lv_obj_t* infos[4];
  for (int i = 0; i < 4; i++) {
    rects[i] = lv_obj_create(lv_scr_act());
    names[i] = lv_label_create(lv_scr_act());
    infos[i] = lv_label_create(lv_scr_act());
    lv_obj_set_size(rects[i], lv_pct(100), 25);
    lv_obj_align(rects[i], LV_ALIGN_TOP_LEFT, 0, (lv_coord_t)(0 + i * 25));
    lv_obj_align(names[i], LV_ALIGN_TOP_LEFT, 5, (lv_coord_t)(5 + i * 25));
    lv_obj_align(infos[i], LV_ALIGN_TOP_RIGHT, -(5 - FNT_OFF), (lv_coord_t)(5 + i * 25));
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
  if (select != NULL) {
    lvx_sign_t open = {
        .title = "A",
        .text = select,
        .align = LV_ALIGN_BOTTOM_RIGHT,
    };
    lvx_sign_create(&open, lv_scr_act());
  }

  // add info
  lv_obj_t* info = lv_label_create(lv_scr_act());
  lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -5);

  // end draw
  gfx_end(true, false);

  for (;;) {
    // begin draw
    gfx_begin(false, false);

    // fill list
    for (int i = 0; i < +4; i++) {
      // get index
      int index = *offset + i;

      // handle empty
      if (index >= total) {
        // clear labels and rectangle
        lv_label_set_text(names[i], "");
        lv_label_set_text(infos[i], "");
        lv_obj_set_style_bg_color(rects[i], lv_color_white(), LV_PART_MAIN);

        continue;
      }

      // get item
      gui_list_item_t item = cb(index, ctx);

      // update labels
      lv_label_set_text(names[i], item.title);
      lv_label_set_text(infos[i], item.info);

      // handle selected
      if (index == selected) {
        lv_obj_set_style_text_color(names[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_color(infos[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(rects[i], lv_color_black(), LV_PART_MAIN);
      } else {
        lv_obj_set_style_text_color(names[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_color(infos[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(rects[i], lv_color_white(), LV_PART_MAIN);
      }
    }

    // update info
    lv_label_set_text(info, lvx_fmt("%d/%d", selected + 1, total));

    // end draw
    gfx_end(false, false);

    // await event
    sig_event_t event = sig_await(SIG_UP | SIG_DOWN | SIG_META | SIG_SCROLL, timeout);

    // handle arrows
    if ((event.type & (SIG_UP | SIG_DOWN | SIG_SCROLL)) != 0) {
      if (event.type == SIG_SCROLL) {
        selected += (int)event.scroll.std;
      } else {
        selected += event.type == SIG_UP ? -1 : 1;
      }
      while (selected < 0) {
        selected += total;
      }
      while (selected > total - 1) {
        selected -= total;
      }
      if (selected > *offset + 3) {
        *offset = selected - 3;
      } else if (selected < *offset) {
        *offset = selected;
      }
      continue;
    }

    /* handle meta and timeout */

    // cleanup
    gui_cleanup(false);

    // handle escape and timeout
    if (event.type == SIG_ESCAPE || event.type == SIG_TIMEOUT) {
      return -1;
    }

    /* handle enter */

    return selected;
  }
}

static gui_list_item_t scr_list_strings_cb(int num, void* ctx) {
  // return item
  return (gui_list_item_t){((const char**)ctx)[num], ""};
}

int gui_list_strings(int start, int* offset, const char** strings, const char* select, const char* cancel,
                     int64_t timeout) {
  // count strings
  int total = 0;
  while (strings[total] != NULL) {
    total++;
  }

  // show list
  int ret = gui_list(total, start, offset, select, cancel, scr_list_strings_cb, strings, timeout);

  return ret;
}

bool gui_wheel(const char* title, int32_t* value, int32_t min, int32_t step, int32_t max, const char* ok,
               const char* cancel, const char* format, int64_t timeout) {
  // begin draw
  gfx_begin(false, false);

  // add text
  lv_obj_t* text = lv_label_create(lv_scr_act());
  lv_label_set_text(text, title);
  lv_obj_align(text, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(text, 6, LV_PART_MAIN);

  // add wheel
  lvx_wheel_t wheel = {
      .value = *value,
      .min = min,
      .step = step,
      .max = max,
      .format = format,
      .fixed = false,
  };
  lvx_wheel_create(&wheel, lv_scr_act());
  lv_obj_align(wheel._col, LV_ALIGN_TOP_MID, 0, 50);

  // add signs
  lvx_sign_t sign_a = {
      .title = "A",
      .text = ok,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t sign_b = {
      .title = "B",
      .text = cancel,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&sign_a, lv_scr_act());
  lvx_sign_create(&sign_b, lv_scr_act());

  // focus first wheel
  lvx_wheel_focus(&wheel, true);

  // end draw
  gfx_end(false, false);

  // prepare wheels
  lvx_wheel_t* wheels[] = {&wheel};
  int cur_wheel = 0;

  for (;;) {
    // await event
    sig_event_t event = sig_await(SIG_KEYS | SIG_SCROLL, timeout);

    // apply wheel events
    if (event.type & (SIG_ARROWS | SIG_SCROLL)) {
      gfx_begin(false, false);
      lvx_wheel_group_update(wheels, 1, event, &cur_wheel);
      gfx_end(false, false);
      continue;
    }

    // cleanup
    gui_cleanup(false);

    // handle result
    if (event.type == SIG_ENTER) {
      *value = wheel.value;
      return true;
    }

    return false;
  }
}

void gui_cycle(bool small, const char** texts, const char* next, const char* back) {
  // count texts
  int num = 0;
  while (texts[num] != NULL) {
    num++;
  }

  // prepare position
  int pos = 0;

  // begin draw
  gfx_begin(false, false);

  // add text
  lv_obj_t* text = lv_label_create(lv_scr_act());
  lv_obj_align(text, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(text, 6, LV_PART_MAIN);
  if (small) {
    lv_obj_set_style_text_font(text, &fnt_8, LV_PART_MAIN);
  }

  // add info
  lv_obj_t* info = lv_label_create(lv_scr_act());
  lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -5);

  // add signs
  lvx_sign_t sign_a = {
      .title = "A",
      .text = next,
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t sign_b = {
      .title = "B",
      .text = back,
      .align = LV_ALIGN_BOTTOM_LEFT,
  };
  lvx_sign_create(&sign_a, lv_scr_act());
  lvx_sign_create(&sign_b, lv_scr_act());

  // end draw
  gfx_end(false, false);

  for (;;) {
    // begin draw
    gfx_begin(false, false);

    // update text
    lv_label_set_text(text, texts[pos]);

    // update info
    lv_label_set_text(info, lvx_fmt("%d/%d", pos + 1, num));

    // end draw
    gfx_end(false, false);

    // await event
    sig_event_t event = sig_await(SIG_KEYS, 0);

    // handle navigation
    if (event.type & (SIG_ENTER | SIG_DOWN | SIG_RIGHT) && pos < num - 1) {
      pos++;
      continue;
    } else if (event.type & (SIG_ESCAPE | SIG_UP | SIG_LEFT) && pos > 0) {
      pos--;
      continue;
    }

    // cleanup
    gui_cleanup(false);

    return;
  }
}
