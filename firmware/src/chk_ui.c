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
#include <al/clock.h>
#include <al/sensor.h>
#include <al/store.h>

#include <stdio.h>

#include "chk.h"
#include "scr.h"
#include "chk_code.h"
#include "chk_store.h"
#include "fnt.h"
#include "gfx.h"
#include "gui.h"
#include "img.h"
#include "lvx.h"
#include "sig.h"

static uint32_t chk_device_tag(void);

// Below this cadence a deep sleep is not worth a reset cycle, so a check
// simply stays awake. The trial's checks sample every five seconds and never
// sleep; the long condition checks, which sample every minute or slower, spend
// nearly all their time asleep.
#define CHK_SLEEP_MIN_S 30

// How long a prompt sleeps before waking to look for its key again. The panel
// holds the prompt through the sleep and a key wakes the device at once, so
// the timer wake is only there to keep the stores moving.
#define CHK_PARK_MS (30 * 60 * 1000)

// the screen the running flow is on, or NULL when no flow runs
static void *chk_park_screen = NULL;

void chk_park_into(void *screen) {
  chk_park_screen = screen;
}

// Waits for a prompt's key and maps it onto an outcome. Inaction is read two
// ways: on a check nobody has started the user was only looking, so the
// prompt gives the device back; on a started check they are waiting for it,
// so the prompt stays until it is dismissed, sleeping through the wait where
// the cadence allows. A sleep is a reset that re-enters the flow at the step
// it was on; a refused one returns here to keep waiting awake.
static chk_result_t chk_prompt_await(int32_t timeout) {
  for (;;) {
    sig_event_t event = gui_await(SIG_META, timeout);
    if (event.type & SIG_ENTER) {
      return CHK_NEXT;
    } else if (event.type & SIG_ESCAPE) {
      return CHK_BACK;
    } else if (!(event.type & SIG_TIMEOUT)) {
      return CHK_EXIT;
    } else if (chk_park_screen == NULL || !chk_started(chk_context())) {
      return CHK_IDLE;
    }
    int interval = chk_cadence();
    if (interval >= CHK_SLEEP_MIN_S) {
      scr_park(interval, CHK_PARK_MS, chk_park_screen);
    }
  }
}

// the header every non-dialogue check screen carries: the check on the left,
// the stage on the right, a rule beneath. It spans `from` to `to`, so a
// screen with a column spoken for keeps its header to the other one.
static void chk_chrome_span(lv_coord_t from, lv_coord_t to, const char *title, const char *stage) {
  // add title
  lv_obj_t *lbl = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(lbl, &fnt_8, LV_PART_MAIN);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, (lv_coord_t)(from + 8), 6);
  lv_label_set_text(lbl, title);

  // add stage
  lv_obj_t *right = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(right, &fnt_8, LV_PART_MAIN);
  lv_obj_align(right, LV_ALIGN_TOP_RIGHT, (lv_coord_t)(to - 296 - 8), 6);
  lv_label_set_text(right, stage);

  // add rule
  lv_obj_t *line = lv_obj_create(lv_scr_act());
  lv_obj_align(line, LV_ALIGN_TOP_LEFT, from, 19);
  lv_obj_set_width(line, (lv_coord_t)(to - from));
  lv_obj_set_height(line, 1);
  lv_obj_set_style_border_width(line, 1, LV_PART_MAIN);
  lv_obj_set_style_border_side(line, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
  lv_obj_set_style_border_color(line, lv_color_black(), LV_PART_MAIN);
}

void chk_chrome(const char *title, const char *stage) {
  chk_chrome_span(0, 296, title, stage);
}

// how far robin and his bubble stand above their menu-screen spot, which
// clears the bottom edge for the two signs
#define CHK_BUBBLE_LIFT (-20)

// one bubble, awaited
static chk_result_t chk_say_one(const chk_bubble_t *bubble) {
  // begin draw
  gfx_begin(false, false);

  // add robin, standing as he does on the menu screen but a sign's height
  // up, on the same spot whatever the bubble says, so he does not hop about
  // as the text changes
  lv_obj_t *robin = lv_img_create(lv_scr_act());
  lv_img_set_src(robin, bubble->mood);
  lv_obj_align(robin, LV_ALIGN_BOTTOM_LEFT, 20, -10 + CHK_BUBBLE_LIFT);

  // add bubble, lifted with him and growing upwards
  lvx_bubble_t frame_obj = {.text = bubble->text};
  lvx_bubble_create(&frame_obj, lv_scr_act());
  lvx_bubble_update(&frame_obj);
  lv_obj_align(frame_obj._frame, LV_ALIGN_BOTTOM_LEFT, 60, -30 + CHK_BUBBLE_LIFT);
  lv_obj_align(frame_obj._label, LV_ALIGN_BOTTOM_LEFT, 76, -38 + CHK_BUBBLE_LIFT);

  // add signs along the bottom edge
  lvx_sign_t sign = {
      .title = "A",
      .text = bubble->action != NULL ? bubble->action : CHK_TEXT(next),
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t back = {.title = "B", .text = CHK_TEXT(back), .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_create(&sign, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // await the key
  chk_result_t result = chk_prompt_await(CHK_ACTION_TIMEOUT);

  // cleanup
  gui_cleanup(false);

  return result;
}

chk_result_t chk_say_from(const chk_bubble_t *bubbles, size_t count, size_t start) {
  size_t i = start < count ? start : 0;
  for (;;) {
    chk_result_t result = chk_say_one(&bubbles[i]);
    if (result == CHK_NEXT) {
      if (++i == count) {
        return CHK_NEXT;
      }
    } else if (result == CHK_BACK) {
      if (i == 0) {
        return CHK_BACK;
      }
      i--;
    } else {
      return result;
    }
  }
}

chk_result_t chk_say(const chk_bubble_t *bubbles, size_t count) {
  return chk_say_from(bubbles, count, 0);
}

// the most items a checklist holds
#define CHK_LIST_MAX 8

chk_result_t chk_list(const char *title, const char *stage, const char *const *items, size_t count, bool done) {
  if (count > CHK_LIST_MAX) {
    count = CHK_LIST_MAX;
  }
  size_t ticked = done ? count : 0;

  // begin draw
  gfx_begin(false, false);

  // add chrome
  chk_chrome(title, stage);

  // add items, each with a box the user ticks off with the key
  lv_obj_t *boxes[CHK_LIST_MAX];
  for (size_t i = 0; i < count; i++) {
    boxes[i] = lv_obj_create(lv_scr_act());
    lv_obj_align(boxes[i], LV_ALIGN_TOP_LEFT, 10, (lv_coord_t)(30 + i * 22));
    lv_obj_set_size(boxes[i], 11, 11);
    lv_obj_set_style_radius(boxes[i], 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(boxes[i], 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(boxes[i], lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(boxes[i], lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(boxes[i], i < ticked ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lbl, &fnt_16, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 28, (lv_coord_t)(27 + i * 22));
    lv_label_set_text(lbl, items[i]);
  }

  // add signs
  lvx_sign_t ok = {
      .title = "A",
      .text = ticked == count ? CHK_TEXT(next) : CHK_TEXT(check),
      .align = LV_ALIGN_BOTTOM_RIGHT,
  };
  lvx_sign_t back = {.title = "B", .text = CHK_TEXT(back), .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_create(&ok, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // the key ticks the items off one at a time, and only moves on once every
  // box is filled: the list is there to be read, not skipped
  chk_result_t result;
  for (;;) {
    result = chk_prompt_await(CHK_ACTION_TIMEOUT);
    if (result != CHK_NEXT || ticked == count) {
      break;
    }

    // begin draw
    gfx_begin(false, false);

    // fill the next box
    lv_obj_set_style_bg_opa(boxes[ticked], LV_OPA_COVER, LV_PART_MAIN);
    ticked++;

    // relabel the key once the list is done
    if (ticked == count) {
      lvx_sign_set_text(&ok, CHK_TEXT(next));
    }

    // end draw
    gfx_end(false, false);
  }

  // cleanup
  gui_cleanup(false);

  return result;
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

  // add note, which says what the numbers above are rather than what they
  // mean, between the signs
  if (note != NULL) {
    lv_obj_t *lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lbl, &fnt_8, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_label_set_text(lbl, note);
  }

  // add signs
  lvx_sign_t sign = {.title = "A", .text = CHK_TEXT(done), .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_t back = {.title = "B", .text = CHK_TEXT(back), .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_create(&sign, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw
  gfx_end(false, false);

  // await the key
  chk_result_t result = chk_prompt_await(CHK_ACTION_TIMEOUT);

  // cleanup
  gui_cleanup(false);

  return result;
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

// Folds in every reading taken since the last one this check saw, and reports
// how many there were and what the newest was.
//
// Awake this is normally a single sample; after a sleep it is everything the
// ULP gathered while the device was off, which is the whole reason a check may
// sleep at all. A measurement that only ever looked at the newest reading
// would silently drop a sleep's worth of them.
static int chk_catch_up(chk_t *c, const chk_screen_t *screen, int64_t began, chk_run_state_t *state, float *latest) {
  al_sample_source_t src = al_store_source();
  al_sample_info_t info = src.info(src.ctx);

  // land on the first reading this check has not folded in, rather than
  // walking the whole store past everything it has
  int first = chk_first_after(&src, c->seen);
  if (first < 0) {
    return 0;
  }

  int taken = 0;
  for (size_t i = (size_t)first; i < info.count && *state == CHK_RUN_GO; i++) {
    al_sample_t sample;
    src.read(src.ctx, &sample, 1, i);

    // skip anything this check has already folded in
    int64_t at = info.start + sample.off;
    if (at <= c->seen) {
      continue;
    }
    c->seen = at;

    // and anything from before the run began
    int32_t elapsed = (int32_t)(at - began);
    if (elapsed < 0) {
      continue;
    }

    float value = al_sample_valid(sample) ? al_sample_read(sample, screen->field) : NAN;
    bool valid = !isnan(value);
    if (valid) {
      *latest = value;
    }

    chk_step_t verdict = CHK_STEP_WAIT;
    if (valid && screen->on_sample != NULL) {
      verdict = screen->on_sample(c, value, elapsed);
    }

    // keep a bar for the slot this reading falls in
    if (valid) {
      int slot = elapsed / CHK_SLOT_MS;
      if (slot >= CHK_SLOTS) {
        slot = CHK_SLOTS - 1;
      }
      chk_bars[slot] = value;
      if (slot >= chk_bar_count) {
        chk_bar_count = slot + 1;
      }
    }

    *state = chk_measure_step(&screen->cfg, &c->run, elapsed, valid, verdict);
    taken++;
  }

  return taken;
}

int chk_cadence(void) {
  // the sensor's own cadence, not the interval the stores migrate at: the
  // latter is never below thirty seconds, whatever the sensor is doing
  int interval = (int)al_sensor_get_interval();
  return interval > 0 ? interval : 5;
}

// the samples a record is built from, on their way between the store and flash
static float chk_samples[CHK_CODE_MAX_SAMPLES];

// Copies every reading taken since the last one this check kept out of the
// short store and onto the check's record, opening the record for the first.
// Called as each measurement ends, so no stretch of a check is older than the
// ring by the time it is copied. What the ring has already let go is gone: the
// long store's coarser samples are no substitute for it, so they are not
// taken.
static size_t chk_keep(chk_t *c, al_sample_field_t signal) {
  // a record that is no longer open is sealed or lost, and either way this
  // check has nothing more to add to it
  if (c->file != 0 && chk_store_pending() != c->file) {
    return 0;
  }

  al_sample_source_t src = al_store_source();
  al_sample_info_t info = src.info(src.ctx);

  // everything after the last sample kept, or after the check began
  int64_t since = c->kept != 0 ? c->kept : c->start;
  int first = chk_first_after(&src, since);
  if (first < 0) {
    return 0;
  }

  // the combined source lists the long store ahead of the short one
  int long_count = (int)al_store_count(AL_STORE_LONG);
  if (first < long_count) {
    first = long_count;
  }

  size_t have = 0;
  for (size_t i = (size_t)first; i < info.count && have < CHK_CODE_MAX_SAMPLES; i++) {
    al_sample_t sample;
    src.read(src.ctx, &sample, 1, i);
    int64_t at = info.start + sample.off;
    if (at <= since) {
      continue;
    }
    c->kept = at;

    // a reading the sensor could not give is left out, as it always was
    if (!al_sample_valid(sample)) {
      continue;
    }
    float value = al_sample_read(sample, signal);
    if (isnan(value)) {
      continue;
    }
    chk_samples[have++] = value;
  }
  if (have == 0) {
    naos_log("chk: nothing to keep since %lld (first=%d long=%d count=%u)", since, first, long_count, info.count);
    return 0;
  }

  // open the record on the first samples worth keeping
  if (c->file == 0) {
    c->file = chk_store_open(c, (uint8_t)signal, (uint8_t)chk_cadence());
    if (c->file == 0) {
      naos_log("chk: could not open a record");
      return 0;
    }
  }

  size_t took = chk_store_append(c->file, chk_samples, have);
  naos_log("chk: kept %u of %u samples onto record %u", took, have, c->file);

  return took;
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

chk_result_t chk_measure(chk_t *c, const chk_screen_t *screen) {
  // prepare the canvas once and keep it: a check may run many times
  if (screen->show == CHK_SHOW_CHART && chk_canvas_buffer == NULL) {
    chk_canvas_buffer = al_calloc(1, LV_CANVAS_BUF_SIZE_TRUE_COLOR(280, 50));
  }

  // the run lives in the context so it survives a sleep. One that has been
  // entered before is continued from where it stopped; a fresh one is timed
  // from now and ignores everything already in the store, which belongs to
  // whatever came before it
  chk_measure_run_t *run = &c->run;
  if (run->began == 0) {
    chk_measure_reset(run);
    run->began = al_clock_get_epoch();
    c->seen = run->began;

    // a new run gets a clean chart; a continued one keeps its bars, which a
    // reset would have wiped anyway
    chk_bar_count = 0;
  }
  int64_t began = run->began;

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

  // label the key where the check says what it does
  if (screen->back != NULL) {
    lvx_sign_t back = {.title = "B", .text = screen->back, .align = LV_ALIGN_BOTTOM_LEFT};
    lvx_sign_create(&back, lv_scr_act());
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

  // the cadence the device is sampling at, which decides whether waiting for
  // the next reading is worth a sleep
  int interval = chk_cadence();
  chk_run_state_t state = CHK_RUN_GO;

  for (;;) {
    // fold in everything new before drawing: awake that is the reading just
    // taken, and on re-entry after a sleep it is everything the ULP gathered
    // while the device was off
    chk_catch_up(c, screen, began, &state, &value);
    int32_t elapsed = (int32_t)(al_clock_get_epoch() - began);

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

    // the run has ended, and what ended it is on the screen
    if (state != CHK_RUN_GO) {
      break;
    }

    // wait for the next reading. At a slow cadence that wait is worth a deep
    // sleep: the ULP keeps sampling into the store while the device is off,
    // the panel holds this screen, and waking re-enters the flow at this step.
    // The call returns only when the device has to stay awake after all, in
    // which case the wait happens here instead.
    if (interval >= CHK_SLEEP_MIN_S) {
      scr_park(interval, interval * 1000, chk_park_screen);
    }

    // the key hands the run back to the flow as it stands, so that a run
    // come back to after a question carries on where it was
    if (chk_await() != CHK_NEXT) {
      gui_cleanup(false);
      return CHK_BACK;
    }
  }

  // cleanup
  gui_cleanup(false);

  // a run nobody could measure is not a result, and nothing of it is kept
  if (state == CHK_RUN_FAILED) {
    chk_restart(c);
    chk_bubble_t sorry = {.mood = &img_robin_standing, .text = CHK_TEXT(sensor_errors), .action = CHK_TEXT(ok)};
    chk_result_t said = chk_say(&sorry, 1);
    return said == CHK_NEXT ? CHK_AGAIN : said;
  }

  // keep what the store holds now, while it still does
  chk_keep(c, screen->field);

  return CHK_NEXT;
}

// The symbol is drawn at two pixels a module with the quiet zone inside the
// margin: the 296x128 panel takes a version 9 symbol that way, which is the
// 153-byte budget the payload is written to. A phone read every candidate in
// the device test, so two pixels is the conservative choice rather than the
// limit.
#define CHK_QR_PX 2

// the column the symbol's canvas takes, quiet zone included
#define CHK_QR_SIDE 128

static uint8_t chk_qr_symbol[CHK_CODE_QR_BUFFER_LEN];
static lv_color_t *chk_qr_canvas_buffer;

chk_result_t chk_qr(const char *title, const char *digits, const char *caption) {
  // encode the link, which the codec does so the host tests can hold it to
  // an independent encoding
  bool ok = chk_code_symbol(digits, chk_qr_symbol);

  // begin draw, as a full refresh: a symbol over the ghost of the screen
  // before it does not scan as well as one on clean white
  gfx_begin(true, false);

  // the symbol and its quiet zone take the right column to within three
  // pixels of the top, so the header keeps to the left of it, where the keys
  // are too
  chk_chrome_span(0, ok ? 296 - CHK_QR_SIDE : 296, title, CHK_TEXT(stage__share));

  if (!ok) {
    // the result did not fit a symbol the panel can show, which is a bug
    // rather than something the user did
    lv_obj_t *lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lbl, &fnt_16, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lbl, CHK_TEXT(share_failed));
  } else {
    int size = qrcodegen_getSize(chk_qr_symbol);
    int side = size * CHK_QR_PX;

    // keep the canvas once: a check may be shared more than once
    if (chk_qr_canvas_buffer == NULL) {
      chk_qr_canvas_buffer = al_calloc(1, LV_CANVAS_BUF_SIZE_TRUE_COLOR(128, 128));
    }

    lv_obj_t *canvas = lv_canvas_create(lv_scr_act());
    memset(chk_qr_canvas_buffer, 0, LV_CANVAS_BUF_SIZE_TRUE_COLOR(128, 128));
    lv_canvas_set_buffer(canvas, chk_qr_canvas_buffer, 128, 128, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_RIGHT, 0, 0);

    // the white ground around the symbol is the quiet zone
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_black();
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = 0;

    // centre the symbol, leaving at least four modules of quiet zone
    lv_coord_t off = (lv_coord_t)((128 - side) / 2);

    // merge dark modules into horizontal runs, which is far fewer draws than
    // one rectangle per module
    for (int y = 0; y < size; y++) {
      int x = 0;
      while (x < size) {
        if (!qrcodegen_getModule(chk_qr_symbol, x, y)) {
          x++;
          continue;
        }
        int run = 0;
        while (x + run < size && qrcodegen_getModule(chk_qr_symbol, x + run, y)) {
          run++;
        }
        lv_canvas_draw_rect(canvas, (lv_coord_t)(off + x * CHK_QR_PX), (lv_coord_t)(off + y * CHK_QR_PX),
                            (lv_coord_t)(run * CHK_QR_PX), CHK_QR_PX, &dsc);
        x += run;
      }
    }

    // the caption sits beside the symbol, not under it
    lv_obj_t *lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lbl, &fnt_16, LV_PART_MAIN);
    lv_obj_set_width(lbl, 296 - CHK_QR_SIDE - 16);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 34);
    lv_label_set_text(lbl, caption != NULL ? caption : CHK_TEXT(share_scan));
  }

  // add signs, both keeping to the left column while the symbol has the right
  lvx_sign_t sign = {
      .title = "A",
      .text = CHK_TEXT(done),
      .align = LV_ALIGN_BOTTOM_RIGHT,
      .shift = ok ? -CHK_QR_SIDE : 0,
  };
  lvx_sign_t back = {.title = "B", .text = CHK_TEXT(back), .align = LV_ALIGN_BOTTOM_LEFT};
  lvx_sign_create(&sign, lv_scr_act());
  lvx_sign_create(&back, lv_scr_act());

  // end draw and wait for the panel: the first flag skips the update, which
  // is the layout pass other screens take and not what a symbol wants
  gfx_end(false, true);

  // await the key, giving the user time to actually scan it
  chk_result_t result = chk_prompt_await(CHK_ACTION_TIMEOUT * 3);

  // cleanup
  gui_cleanup(false);

  return result;
}

// The payload bytes a 2 px symbol carries behind the prefix at level M, which
// is what the panel height allows rather than what the format could hold.
#define CHK_SHARE_MAX_BYTES 153

// The payload carries twenty-four bits of device, shown on the page as a hex
// tag. `device-id` is a hex string and the device name is "AL" plus its last
// six characters, so taking those six puts the name on the page exactly as the
// user reads it on their own device.
static uint32_t chk_device_tag(void) {
  const char *id = naos_get_s("device-id");
  size_t len = id != NULL ? strlen(id) : 0;
  if (len < 6) {
    return 0;
  }

  uint32_t tag = 0;
  for (const char *p = id + len - 6; *p != '\0'; p++) {
    int digit;
    if (*p >= '0' && *p <= '9') {
      digit = *p - '0';
    } else if (*p >= 'a' && *p <= 'f') {
      digit = *p - 'a' + 10;
    } else if (*p >= 'A' && *p <= 'F') {
      digit = *p - 'A' + 10;
    } else {
      return 0;  // not hex after all, so there is no tag worth showing
    }
    tag = (tag << 4) | (uint32_t)digit;
  }

  return tag;
}

// the stored check with this number, or NULL
static chk_store_file_t *chk_find(uint16_t num) {
  for (size_t i = 0; i < chk_store_count(); i++) {
    chk_store_file_t *candidate = chk_store_get(i);
    if (candidate != NULL && candidate->head.num == num) {
      return candidate;
    }
  }
  return NULL;
}

bool chk_view_of(uint16_t num, chk_view_t *out) {
  chk_store_file_t *file = chk_find(num);
  if (file == NULL) {
    return false;
  }
  return chk_describe(file->head.check, file->head.result, file->head.bounds, CHK_MARKS, file->head.cadence, out);
}

uint16_t chk_record(chk_t *c, al_sample_field_t signal) {
  if (c == NULL) {
    return 0;
  }

  // sealed already: a result step re-entered after a timeout shows the same
  // record rather than writing another
  if (c->file != 0 && chk_store_pending() != c->file) {
    return c->file;
  }

  // the tail since the last measurement ended, then the seal
  chk_keep(c, signal);
  if (c->file == 0) {
    naos_log("chk: no record to seal");
    return 0;
  }
  if (!chk_store_finish(c->file, c)) {
    naos_log("chk: record %u refused the seal", c->file);
    c->file = 0;
    return 0;
  }
  naos_log("chk: sealed record %u", c->file);

  return c->file;
}

void chk_release(chk_t *c) {
  // a record the check did not finish goes with it
  if (c->file != 0) {
    chk_store_discard(c->file);
  }
  chk_end(c);

  // nothing is left to park into
  chk_park_screen = NULL;
}

bool chk_underway(const chk_t *c) {
  return chk_started(c) && (c->file == 0 || chk_store_pending() == c->file);
}

bool chk_confirm_stop(void) {
  return gui_choose(CHK_TEXT(stop), CHK_TEXT(carry_on), true, CHK_ACTION_TIMEOUT) == 1;
}

bool chk_confirm_discard(void) {
  return gui_choose(CHK_TEXT(discard), CHK_TEXT(carry_on), true, CHK_ACTION_TIMEOUT) == 1;
}

void chk_restart(chk_t *c) {
  // the record so far goes, it holds what is being redone
  if (c->file != 0) {
    chk_store_discard(c->file);
  }

  // begin afresh, keeping where the flow is and what the user has entered
  uint8_t id = c->id;
  uint8_t step = c->step;
  float result[CHK_RESULTS];
  memcpy(result, c->result, sizeof(result));
  chk_begin(c, id);
  c->step = step;
  memcpy(c->result, result, sizeof(result));
}

chk_result_t chk_show_code(uint16_t num) {
  chk_store_file_t *file = chk_find(num);

  chk_view_t view;
  if (file == NULL || !chk_view_of(num, &view)) {
    naos_log("chk: no record %u to share", num);
    chk_bubble_t sorry = {.mood = &img_robin_standing, .text = CHK_TEXT(share_failed), .action = CHK_TEXT(ok)};
    return chk_say(&sorry, 1);
  }

  // the samples come off flash, not out of the device store, which may have
  // turned over long ago
  size_t have = chk_store_samples(num, chk_samples, CHK_CODE_MAX_SAMPLES);
  if (have < 2) {
    naos_log("chk: record %u holds %u samples", num, have);
    chk_bubble_t sorry = {.mood = &img_robin_standing, .text = CHK_TEXT(share_failed), .action = CHK_TEXT(ok)};
    return chk_say(&sorry, 1);
  }

  uint8_t cadence = 2;
  for (uint8_t i = 0; i < 16; i++) {
    if (chk_code_cadences[i] == file->head.cadence) {
      cadence = i;
      break;
    }
  }

  chk_code_meta_t meta = {
      .check = view.check,
      .minute = (uint32_t)((file->head.start - 1735689600000LL) / 60000),
      .offset = file->head.offset,
      .device = chk_device_tag(),
      .room = CHK_CODE_ROOM_NONE,  // the device has no way to know where it stands yet
      .cadence = cadence,
  };

  static char digits[CHK_CODE_MAX_DIGITS];
  if (!chk_code_pack(&meta, view.payload, view.num_payload, chk_samples, have, CHK_SHARE_MAX_BYTES, digits,
                     sizeof(digits), NULL, NULL)) {
    naos_log("chk: record %u (check %u, %u samples, cadence %u) refused by the packer", num, view.check, have,
             file->head.cadence);
    for (size_t i = 0; i < view.num_payload; i++) {
      naos_log("chk:   field %u = %f", i, view.payload[i]);
    }
    chk_bubble_t sorry = {.mood = &img_robin_standing, .text = CHK_TEXT(share_failed), .action = CHK_TEXT(ok)};
    return chk_say(&sorry, 1);
  }

  return chk_qr(view.title, digits, CHK_TEXT(share_scan));
}

chk_result_t chk_reopen(uint16_t num) {
  // the verdict, the stats and the code, the screens the live flow ends on,
  // and the B key walks them backwards. The view is rebuilt for each screen,
  // as the code screen's own view turns the formatter's buffers over.
  int phase = 0;
  bool back = false;
  for (;;) {
    chk_view_t view;
    if (!chk_view_of(num, &view)) {
      return CHK_EXIT;
    }

    chk_result_t result;
    if (phase == 0) {
      result = chk_say_from(view.verdict, view.num_verdict, back ? view.num_verdict - 1 : 0);
      if (result != CHK_NEXT) {
        return result;
      }
      phase = 1;
      back = false;
    } else if (phase == 1) {
      result = chk_stats(view.title, CHK_TEXT(stage__results), view.lines, view.num_lines, view.note);
      if (result == CHK_BACK) {
        phase = 0;
        back = true;
        continue;
      }
      if (result != CHK_NEXT) {
        return result;
      }
      phase = 2;
    } else {
      result = chk_show_code(num);
      if (result != CHK_BACK) {
        return result;
      }
      phase = 1;
    }
  }
}
