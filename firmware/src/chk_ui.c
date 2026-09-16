// The drawing half of the check kit. Kept apart from chk.c so that the
// policy, the fits and the copy stay testable on the host, where none of
// this exists.

#include <math.h>
#include <string.h>

#include <lvgl.h>
#include <naos.h>
#include <naos/sys.h>

#include <al/core.h>
#include <al/utils.h>
#include <al/store.h>

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

// The chart is a display buffer, not the data path: it holds one bar per
// time slot at the resolution the view screen uses, is lossy on purpose, and
// is discarded when the screen goes. What a check keeps lives in its
// accumulators, which is what lets the context survive deep sleep.
#define CHK_SLOT_MS 5000
#define CHK_SLOTS 70

static float chk_bars[CHK_SLOTS];
static int chk_bar_count;
static lv_color_t *chk_canvas_buffer;

// reads the signal a check is watching, NaN when there is nothing to read
static float chk_read(al_sample_field_t field) {
  al_sample_t sample = al_store_last();
  if (!al_sample_valid(sample)) {
    return NAN;
  }
  return al_sample_read(sample, field);
}

// awaits the next reading, or the user giving up
static chk_result_t chk_await(void) {
  for (;;) {
    sig_event_t event = gui_await(SIG_KEYS | SIG_SENSOR, 30 * 1000);
    if (event.type & SIG_ESCAPE) {
      return CHK_EXIT;
    } else if (event.type & (SIG_SENSOR | SIG_TIMEOUT)) {
      return CHK_NEXT;
    }
  }
}

chk_result_t chk_measure(chk_t *c, const chk_screen_t *screen, chk_measure_run_t *run) {
  // prepare the canvas once and keep it: a check may run many times
  if (screen->show == CHK_SHOW_CHART && chk_canvas_buffer == NULL) {
    chk_canvas_buffer = al_calloc(1, LV_CANVAS_BUF_SIZE_TRUE_COLOR(280, 50));
  }

  // clear the run and the bars
  chk_measure_reset(run);
  chk_bar_count = 0;

  // take an opening reading so the screen has something to show
  float value = chk_read(screen->field);

  // begin draw
  gfx_begin(false, false);

  // add chrome
  chk_chrome(screen->title, screen->stage);

  // add the value, which is the one thing always on screen
  lv_obj_t *val = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(val, &fnt_24, LV_PART_MAIN);
  lv_obj_set_height(val, 34);  // fnt_24 descenders reach below the line height
  lv_label_set_text(val, "");

  lv_obj_t *clock = NULL;
  lv_obj_t *hint = NULL;
  lv_obj_t *bar = NULL;
  lv_obj_t *canvas = NULL;
  lv_obj_t *status = NULL;

  if (screen->show == CHK_SHOW_PROGRESS) {
    // a baseline is counting, so centre the value and count beneath it
    lv_obj_align(val, LV_ALIGN_TOP_MID, 0, 46);

    hint = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(hint, &fnt_8, LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 77);
    lv_label_set_text(hint, screen->hint != NULL ? screen->hint : "");

    bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(bar, 200, 12);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_bar_set_range(bar, 0, screen->cfg.capacity > 0 ? screen->cfg.capacity : 1);
  } else {
    // a measurement is a curve, so the value sits left with the clock right
    lv_obj_align(val, LV_ALIGN_TOP_LEFT, 8, 29);

    clock = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(clock, &fnt_24, LV_PART_MAIN);
    lv_obj_align(clock, LV_ALIGN_TOP_RIGHT, -8, 30);

    canvas = lv_canvas_create(lv_scr_act());
    memset(chk_canvas_buffer, 0, LV_CANVAS_BUF_SIZE_TRUE_COLOR(280, 50));
    lv_canvas_set_buffer(canvas, chk_canvas_buffer, 280, 50, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 8, 66);
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

    status = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(status, &fnt_8, LV_PART_MAIN);
    lv_obj_align(status, LV_ALIGN_BOTTOM_LEFT, 8, -3);
  }

  // end draw
  gfx_end(true, false);

  // size the chart from the opening reading unless the check said otherwise
  float range = screen->range;
  if (range <= 0) {
    range = !isnan(value) ? (value - screen->floor) * 1.05f : 50;
  }
  if (range < 50) {
    range = 50;
  }

  int64_t began = naos_millis();
  chk_run_state_t state = CHK_RUN_GO;

  while (state == CHK_RUN_GO) {
    int32_t elapsed = (int32_t)(naos_millis() - began);

    // begin draw
    gfx_begin(false, false);

    // update the value and the clock
    lv_label_set_text(val, !isnan(value) ? lvx_fmt("%.0f %s", value, screen->unit) : "");
    if (clock != NULL) {
      lv_label_set_text(clock, lvx_fmt("%d:%02d", elapsed / 60000, elapsed / 1000 % 60));
    }

    // update the progress or the chart
    if (bar != NULL) {
      lv_bar_set_value(bar, run->count, LV_ANIM_OFF);
    } else {
      lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
      lv_draw_line_dsc_t dsc;
      lv_draw_line_dsc_init(&dsc);
      dsc.width = 2;
      for (int i = 0; i < chk_bar_count; i++) {
        float above = chk_bars[i] - screen->floor;
        if (above < 0) {
          above = 0;
        }
        lv_coord_t h = (lv_coord_t)(2 + al_safe_map(above, 0, range, 0, 46));
        lv_point_t points[2] = {
            {.x = (lv_coord_t)(1 + i * 4), .y = 48},
            {.x = (lv_coord_t)(1 + i * 4), .y = (lv_coord_t)(48 - h)},
        };
        lv_canvas_draw_line(canvas, points, 2, &dsc);
      }
    }

    // say something only while nothing is happening
    if (status != NULL) {
      lv_label_set_text(status, run->nudging && screen->nudge != NULL ? screen->nudge : "");
    }

    // end draw
    gfx_end(false, false);

    // await the next reading
    if (chk_await() != CHK_NEXT) {
      gui_cleanup(false);
      return CHK_EXIT;
    }
    elapsed = (int32_t)(naos_millis() - began);

    // take it
    value = chk_read(screen->field);
    bool valid = !isnan(value);

    // let the check make of it what it will
    chk_step_t verdict = CHK_STEP_WAIT;
    if (valid) {
      verdict = screen->on_sample != NULL ? screen->on_sample(c, value, elapsed) : CHK_STEP_WAIT;

      // keep a bar for the slot this reading falls in, overwriting the slot
      // when several land in one
      int slot = elapsed / CHK_SLOT_MS;
      if (slot >= CHK_SLOTS) {
        slot = CHK_SLOTS - 1;
      }
      chk_bars[slot] = value;
      if (slot >= chk_bar_count) {
        chk_bar_count = slot + 1;
      }
    }

    // and let the policy decide what happens next
    state = chk_measure_step(&screen->cfg, run, elapsed, valid, verdict);
  }

  // cleanup
  gui_cleanup(false);

  // a run nobody could measure is not a result
  if (state == CHK_RUN_FAILED) {
    chk_bubble_t sorry = {.mood = &img_robin_standing, .text = CHK_TEXT(sensor_errors), .action = CHK_TEXT(ok)};
    chk_result_t said = chk_say(&sorry, 1);
    return said == CHK_NEXT ? CHK_AGAIN : said;
  }

  return CHK_NEXT;
}
