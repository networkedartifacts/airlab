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
#include <al/store.h>

#include <stdio.h>

#include "qrcodegen.h"

#include "chk.h"
#include "chk_code.h"
#include "chk_store.h"
#include "fnt.h"
#include "gfx.h"
#include "gui.h"
#include "img.h"
#include "lvx.h"
#include "sig.h"

// maps a key press or the lack of one onto an outcome
static uint16_t chk_device_tag(void);

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

// The symbol is drawn at two pixels a module with the quiet zone inside the
// margin: the 296x128 panel takes a version 9 symbol that way, which is the
// 153-byte budget the payload is written to. A phone read every candidate in
// the device test, so two pixels is the conservative choice rather than the
// limit.
#define CHK_QR_PX 2
#define CHK_QR_MAX_VERSION 9

static uint8_t chk_qr_temp[qrcodegen_BUFFER_LEN_FOR_VERSION(CHK_QR_MAX_VERSION)];
static uint8_t chk_qr_out[qrcodegen_BUFFER_LEN_FOR_VERSION(CHK_QR_MAX_VERSION)];
static lv_color_t *chk_qr_canvas_buffer;

chk_result_t chk_qr(const char *title, char letter, const char *digits, const char *caption) {
  // build the link as two segments: the prefix and letter in byte mode, the
  // payload in numeric mode. Encoding the whole string instead would need a
  // version 14 symbol, which does not fit the panel at this module size.
  char head[sizeof(CHK_CODE_PREFIX) + 1];
  snprintf(head, sizeof(head), "%s%c", CHK_CODE_PREFIX, letter);

  struct qrcodegen_Segment segs[2];
  segs[0] = qrcodegen_makeBytes((const uint8_t *)head, strlen(head), chk_qr_temp);
  segs[1] = qrcodegen_makeNumeric(digits, chk_qr_temp + qrcodegen_BUFFER_LEN_FOR_VERSION(CHK_QR_MAX_VERSION) / 2);

  bool ok = qrcodegen_encodeSegmentsAdvanced(segs, 2, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                             CHK_QR_MAX_VERSION, qrcodegen_Mask_AUTO, true, chk_qr_out, chk_qr_out);

  // begin draw
  gfx_begin(false, false);

  // add chrome
  chk_chrome(title, CHK_TEXT(stage__share));

  if (!ok) {
    // the result did not fit a symbol the panel can show, which is a bug
    // rather than something the user did
    lv_obj_t *lbl = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(lbl, &fnt_16, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lbl, CHK_TEXT(share_failed));
  } else {
    int size = qrcodegen_getSize(chk_qr_out);
    int side = size * CHK_QR_PX;

    // keep the canvas once: a check may be shared more than once
    if (chk_qr_canvas_buffer == NULL) {
      chk_qr_canvas_buffer = al_calloc(1, LV_CANVAS_BUF_SIZE_TRUE_COLOR(128, 128));
    }

    lv_obj_t *canvas = lv_canvas_create(lv_scr_act());
    memset(chk_qr_canvas_buffer, 0, LV_CANVAS_BUF_SIZE_TRUE_COLOR(128, 128));
    lv_canvas_set_buffer(canvas, chk_qr_canvas_buffer, 128, 128, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 0);

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
        if (!qrcodegen_getModule(chk_qr_out, x, y)) {
          x++;
          continue;
        }
        int run = 0;
        while (x + run < size && qrcodegen_getModule(chk_qr_out, x + run, y)) {
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
    lv_obj_set_width(lbl, 296 - 136);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 136, 34);
    lv_label_set_text(lbl, caption != NULL ? caption : CHK_TEXT(share_scan));
  }

  // add sign
  lvx_sign_t sign = {.title = "A", .text = CHK_TEXT(done), .align = LV_ALIGN_BOTTOM_RIGHT};
  lvx_sign_create(&sign, lv_scr_act());

  // end draw, refreshing fully: a half-drawn symbol does not scan
  gfx_end(true, false);

  // await the key, giving the user time to actually scan it
  sig_event_t event = gui_await(SIG_META, CHK_ACTION_TIMEOUT * 3);

  // cleanup
  gui_cleanup(false);

  return chk_outcome(event.type);
}

// The budget the 2 px symbol allows behind the prefix at level M, which the
// research measured rather than derived.
#define CHK_SHARE_MAX_BYTES 153

// The payload carries sixteen bits of device, shown on the page as a hex tag.
// `device-id` is a hex string and the device name is "AL" plus its last six
// characters, so taking the last four puts a tag on the page that the user can
// match against the name on their own device.
static uint16_t chk_device_tag(void) {
  const char *id = naos_get_s("device-id");
  size_t len = id != NULL ? strlen(id) : 0;
  if (len < 4) {
    return 0;
  }

  uint16_t tag = 0;
  for (const char *p = id + len - 4; *p != '\0'; p++) {
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
    tag = (uint16_t)((tag << 4) | digit);
  }

  return tag;
}

uint16_t chk_record(const chk_t *c, al_sample_field_t signal, int32_t span_ms) {
  static float samples[CHK_CODE_MAX_SAMPLES];

  if (c == NULL) {
    return 0;
  }

  // read the window the check spanned back out of the device's own store,
  // oldest first: a check keeps accumulators rather than a sample stream, so
  // this is where the curve comes from. The store is a ring that will turn
  // over, so this is the only chance to take a copy.
  int interval = al_store_get_interval();
  if (interval <= 0) {
    interval = 5;
  }
  size_t want = (size_t)(span_ms / 1000 / interval) + 1;
  if (want > CHK_CODE_MAX_SAMPLES) {
    want = CHK_CODE_MAX_SAMPLES;
  }

  size_t have = 0;
  for (size_t i = 0; i < want; i++) {
    al_sample_t sample = al_store_get(AL_STORE_SHORT, -(int)(want - i));
    if (!al_sample_valid(sample)) {
      continue;
    }
    float value = al_sample_read(sample, signal);
    if (!isnan(value)) {
      samples[have++] = value;
    }
  }

  // a curve needs at least two points
  if (have < 2) {
    return 0;
  }

  return chk_store_write(c, (uint8_t)signal, (uint8_t)interval, samples, have);
}

chk_result_t chk_show_code(uint16_t num) {
  // find the stored check
  chk_store_file_t *file = NULL;
  for (size_t i = 0; i < chk_store_count(); i++) {
    chk_store_file_t *candidate = chk_store_get(i);
    if (candidate != NULL && candidate->head.num == num) {
      file = candidate;
      break;
    }
  }

  chk_view_t view;
  if (file == NULL || !chk_describe(file->head.check, file->head.result, &view)) {
    chk_bubble_t sorry = {.mood = &img_robin_standing, .text = CHK_TEXT(share_failed), .action = CHK_TEXT(ok)};
    return chk_say(&sorry, 1);
  }

  // the samples come off flash, not out of the device store, which may have
  // turned over long ago
  static float samples[CHK_CODE_MAX_SAMPLES];
  size_t have = chk_store_samples(num, samples, CHK_CODE_MAX_SAMPLES);
  if (have < 2) {
    chk_bubble_t sorry = {.mood = &img_robin_standing, .text = CHK_TEXT(share_failed), .action = CHK_TEXT(ok)};
    return chk_say(&sorry, 1);
  }

  uint8_t cadence = 2;
  for (uint8_t i = 0; i < 8; i++) {
    if (chk_code_cadences[i] == file->head.cadence) {
      cadence = i;
      break;
    }
  }

  chk_code_meta_t meta = {
      .minute = (uint32_t)((file->head.start - 1735689600000LL) / 60000),
      .device = chk_device_tag(),
      .room = 0,  // the device has no way to know where it stands yet
      .cadence = cadence,
  };

  static char digits[CHK_CODE_MAX_DIGITS];
  if (!chk_code_pack(view.letter, &meta, view.payload, view.num_payload, samples, have, CHK_SHARE_MAX_BYTES, digits,
                     sizeof(digits), NULL, NULL)) {
    chk_bubble_t sorry = {.mood = &img_robin_standing, .text = CHK_TEXT(share_failed), .action = CHK_TEXT(ok)};
    return chk_say(&sorry, 1);
  }

  return chk_qr(view.title, view.letter, digits, CHK_TEXT(share_scan));
}

chk_result_t chk_reopen(uint16_t num) {
  // find the stored check
  chk_store_file_t *file = NULL;
  for (size_t i = 0; i < chk_store_count(); i++) {
    chk_store_file_t *candidate = chk_store_get(i);
    if (candidate != NULL && candidate->head.num == num) {
      file = candidate;
      break;
    }
  }

  chk_view_t view;
  if (file == NULL || !chk_describe(file->head.check, file->head.result, &view)) {
    return CHK_EXIT;
  }

  // the stats as they were, rebuilt from the header without the samples
  chk_result_t result = chk_stats(view.title, CHK_TEXT(stage__results), view.lines, view.num_lines, view.note);
  if (result != CHK_NEXT) {
    return result;
  }

  return chk_show_code(num);
}
