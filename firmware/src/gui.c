#include <lvgl.h>
#include <stdio.h>

#include "gui.h"
#include "gfx.h"
#include "lvx.h"
#include "sig.h"
#include "fnt.h"

void gui_cleanup(bool refresh) {
  // clear group and screen
  gfx_begin(refresh, false);
  lv_disp_set_rotation(NULL, LV_DISP_ROT_NONE);
  lv_group_remove_all_objs(gfx_get_group());
  lv_obj_clean(lv_scr_act());
  gfx_end(false, refresh);
}

void gui_write(const char* text) {
  // show message
  gfx_begin(false, false);
  lv_obj_t* lbl = lv_label_create(lv_scr_act());
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_line_space(lbl, 6, LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  gfx_end(false, false);
}

void gui_message(const char* text, uint32_t timeout) {
  // show message
  gui_write(text);

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
        selected += (int)event.scroll;
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
