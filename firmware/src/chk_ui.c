// The drawing half of the check kit. Kept apart from chk.c so that the
// policy, the fits and the copy stay testable on the host, where none of
// this exists.

#include <lvgl.h>

#include "chk.h"
#include "fnt.h"
#include "gfx.h"
#include "gui.h"
#include "img.h"
#include "lvx.h"
#include "sig.h"

// maps a key press or the lack of one onto an outcome
static chk_result_t chk_outcome(sig_type_t event) {
  if (event & SIG_ENTER) {
    return CHK_NEXT;
  } else if (event & SIG_TIMEOUT) {
    return CHK_IDLE;
  }
  return CHK_EXIT;
}

// the header every non-dialogue check screen carries: the check on the left,
// the stage on the right, a rule beneath
void chk_chrome(const char *title, const char *stage) {
  // add title
  lv_obj_t *lbl = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(lbl, &fnt_8, LV_PART_MAIN);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 6);
  lv_label_set_text(lbl, title);

  // add stage
  lv_obj_t *right = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(right, &fnt_8, LV_PART_MAIN);
  lv_obj_align(right, LV_ALIGN_TOP_RIGHT, -8, 6);
  lv_label_set_text(right, stage);

  // add rule
  lv_obj_t *line = lv_obj_create(lv_scr_act());
  lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, 19);
  lv_obj_set_width(line, lv_pct(100));
  lv_obj_set_height(line, 1);
  lv_obj_set_style_border_width(line, 1, LV_PART_MAIN);
  lv_obj_set_style_border_side(line, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
  lv_obj_set_style_border_color(line, lv_color_black(), LV_PART_MAIN);
}

// one bubble, awaited
static chk_result_t chk_say_one(const chk_bubble_t *bubble) {
  // robin and the bubble are anchored to the bottom like on the menu screen,
  // so find the offset that centres the pair vertically (the frame sizes with
  // the text, mirroring lvx_bubble_update)
  lv_point_t size = {0};
  lv_txt_get_size(&size, bubble->text, &fnt_16, 0, 0, 200, 0);
  lv_coord_t frame = size.y >= 48 ? 64 : size.y >= 32 ? 48 : 32;
  lv_coord_t top = frame > 88 ? 10 : 98 - frame;
  lv_coord_t robin_top = 118 - (lv_coord_t)bubble->mood->header.h;
  lv_coord_t delta = (lv_coord_t)((10 - (top < robin_top ? top : robin_top)) / 2);

  // begin draw
  gfx_begin(false, false);

  // add robin
  lv_obj_t *robin = lv_img_create(lv_scr_act());
  lv_img_set_src(robin, bubble->mood);
  lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10 + delta);

  // add bubble
  lvx_bubble_t frame_obj = {.text = bubble->text};
  lvx_bubble_create(&frame_obj, lv_scr_act());
  lvx_bubble_update(&frame_obj);
  lv_obj_align(frame_obj._frame, LV_ALIGN_BOTTOM_LEFT, 60, -30 + delta);
  lv_obj_align(frame_obj._label, LV_ALIGN_BOTTOM_LEFT, 76, -38 + delta);

  // add sign
  lvx_sign_t sign = {
      .title = "A",
      .text = bubble->action != NULL ? bubble->action : CHK_TEXT(next),
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_create(&sign, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // await the key
  sig_event_t event = gui_await(SIG_META, CHK_ACTION_TIMEOUT);

  // cleanup
  gui_cleanup(false);

  return chk_outcome(event.type);
}

chk_result_t chk_say(const chk_bubble_t *bubbles, size_t count) {
  for (size_t i = 0; i < count; i++) {
    chk_result_t result = chk_say_one(&bubbles[i]);
    if (result != CHK_NEXT) {
      return result;
    }
  }
  return CHK_NEXT;
}

chk_result_t chk_list(const char *title, const char *stage, const char *const *items, size_t count) {
  // begin draw
  gfx_begin(false, false);

  // add chrome
  chk_chrome(title, stage);

  // add items, each with a box the user ticks off by reading it
  for (size_t i = 0; i < count; i++) {
    lv_obj_t *box = lv_obj_create(lv_scr_act());
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, 10, (lv_coord_t)(30 + i * 22));
    lv_obj_set_size(box, 11, 11);
    lv_obj_set_style_radius(box, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lbl, &fnt_16, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 28, (lv_coord_t)(27 + i * 22));
    lv_label_set_text(lbl, items[i]);
  }

  // add signs
  lvx_sign_t ok = {.title = "A", .text = CHK_TEXT(check), .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_t back = {.title = "B", .text = CHK_TEXT(back), .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_create(&ok, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // await the key
  sig_event_t event = gui_await(SIG_META, CHK_ACTION_TIMEOUT);

  // cleanup
  gui_cleanup(false);

  return chk_outcome(event.type);
}

chk_result_t chk_stats(const char *title, const char *stage, const char *const *lines, size_t count,
                       const char *note) {
  // begin draw
  gfx_begin(false, false);

  // add chrome
  chk_chrome(title, stage);

  // add lines
  for (size_t i = 0; i < count; i++) {
    lv_obj_t *lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lbl, &fnt_16, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 10, (lv_coord_t)(27 + i * 20));
    lv_label_set_text(lbl, lines[i]);
  }

  // add note, which says what the numbers above are rather than what they mean
  if (note != NULL) {
    lv_obj_t *lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lbl, &fnt_8, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 10, -5);
    lv_label_set_text(lbl, note);
  }

  // add sign
  lvx_sign_t sign = {.title = "A", .text = CHK_TEXT(done), .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_create(&sign, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // await the key
  sig_event_t event = gui_await(SIG_META, CHK_ACTION_TIMEOUT);

  // cleanup
  gui_cleanup(false);

  return chk_outcome(event.type);
}
