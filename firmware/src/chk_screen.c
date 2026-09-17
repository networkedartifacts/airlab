// The checks as the user walks through them, and what a finished one looks
// like. The screens and the policy are the kit's (chk_ui.c); the maths is
// chk_vent.c's and chk_stove.c's; what is here is each flow, its copy, the
// numbers it chooses, and the view it and a reopened record share.

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include <naos.h>

#include <al/buzzer.h>
#include <al/clock.h>
#include <al/store.h>

#include "chk.h"
#include "chk_bath.h"
#include "chk_bedroom.h"
#include "chk_stove.h"
#include "chk_vent.h"
#include "gui.h"
#include "img.h"
#include "lvx.h"
#include "scr.h"

/* Shared */

// Runs a kit call and routes every outcome but "next". The B key goes where
// the step says, `on_back` naming the step or phase before it, or leaves when
// there is nothing to go back to. Going back past the baseline, or leaving a
// check that is under way, is first put as a question, and declining it
// shows the screen again. Every other way out releases the check: escape is
// the user abandoning it, and a timeout only reaches here before the
// baseline, when the user was looking rather than waiting, so the next run
// starts over. A started check never times out: its prompts park the device
// on the flow's screen and wake back into it.
//
// A block rather than a statement, as going back continues the step loop it
// sits in. Expects `c`, `back`, `on_exit`, `on_idle` and `self` in scope.
#define CHK_TRY(expr, on_back)                                            \
  {                                                                       \
    chk_result_t _r = (expr);                                             \
    if (_r == CHK_BACK) {                                                 \
      int _b = (on_back);                                                 \
      if (_b == CHK_WENT) {                                               \
        back = true;                                                      \
        continue;                                                         \
      }                                                                   \
      if (_b == CHK_STAYED || (chk_underway(c) && !chk_confirm_stop())) { \
        back = false;                                                     \
        continue;                                                         \
      }                                                                   \
    }                                                                     \
    if (_r != CHK_NEXT) {                                                 \
      naos_log("chk: leaving step %u on %d", c->step, _r);                \
      chk_release(c);                                                     \
      return _r == CHK_IDLE ? on_idle : _r == CHK_AGAIN ? self : on_exit; \
    }                                                                     \
    back = false;                                                         \
  }

// what the B key did: went somewhere, stayed after a question, or has
// nowhere to go
enum {
  CHK_NOWHERE,
  CHK_WENT,
  CHK_STAYED,
};

// where the B key goes: a step, a step with the check started over once the
// user has agreed to lose the baseline (out of one, or back into one), a
// phase of the result, or out
#define CHK_STEP(s) (c->step = (s), CHK_WENT)
#define CHK_REDO(s)                                                                                               \
  (chk_confirm_discard() ? (naos_log("chk: redo from step %u", c->step), chk_restart(c), c->step = (s), CHK_WENT) \
                         : CHK_STAYED)
#define CHK_PHASE(p) (c->phase = (p), CHK_WENT)
#define CHK_LEAVE CHK_NOWHERE

// Releases the check after a closing bubble and picks where to land: a
// timeout goes idle, "again" restarts when the caller offers a screen for it,
// and anything else leaves.
static void *chk_leave(chk_t *c, chk_result_t said, void *on_exit, void *on_idle, void *again) {
  naos_log("chk: closing at step %u on %d", c->step, said);
  chk_release(c);
  if (said == CHK_IDLE) return on_idle;
  if (said == CHK_NEXT && again != NULL) return again;
  return on_exit;
}

// A counted baseline accumulates nothing: the run's own count ends it, and
// the value is read back from the store afterwards as a median.
static chk_step_t chk_baseline_sample(chk_t *c, float value, int32_t t_ms) {
  (void)c;
  (void)value;
  (void)t_ms;
  return CHK_STEP_WAIT;
}

// The result step is shown in phases, so a wake out of a parked sleep lands
// on the screen that was showing rather than back at the verdict, and the B
// key walks the phases backwards.
enum {
  RESULT_PHASE_VERDICT,
  RESULT_PHASE_STATS,
  RESULT_PHASE_SHARE,
};

// The view of a result just made: read back from the record, so the result
// shown now is built from exactly what a reopened check will be built from
// later, or from the live block when the record could not be kept.
static bool chk_finish_view(const chk_t *c, uint8_t id, uint16_t stored, chk_view_t *view) {
  return chk_view_of(stored, view) ||
         chk_describe(id, c->result, c->marks, CHK_MARKS, (uint8_t)chk_record_cadence(c), view);
}

// The end every solid result shares: the verdict, the stats, then the code,
// the same screens a reopened check shows. The view is built afresh for
// each, as the formatter's buffers turn over under the screens between.
static void *chk_finish(chk_t *c, uint8_t id, uint16_t stored, void *on_exit, void *on_idle, void *self) {
  bool back = false;
  for (;;) {
    chk_view_t view;
    if (!chk_finish_view(c, id, stored, &view)) {
      chk_release(c);
      return on_exit;
    }

    if (c->phase == RESULT_PHASE_VERDICT) {
      CHK_TRY(chk_say_from(view.verdict, view.num_verdict, back ? view.num_verdict - 1 : 0), CHK_LEAVE);
      c->phase = RESULT_PHASE_STATS;
      continue;
    }

    if (c->phase == RESULT_PHASE_STATS) {
      CHK_TRY(chk_stats(view.title, CHK_TEXT(stage__results), view.lines, view.num_lines, view.note),
              CHK_PHASE(RESULT_PHASE_VERDICT));
      c->phase = RESULT_PHASE_SHARE;
      continue;
    }

    CHK_TRY(chk_show_code(stored), CHK_PHASE(RESULT_PHASE_STATS));
    chk_release(c);
    return on_exit;
  }
}

// The sample index a phase boundary falls at: how many samples the series
// holds before that moment, at this cadence. The record has no sample times,
// so this is the grid the page draws the series on; a reading the sensor
// could not give is left out of the record, and each one moves the marks
// after it a sample early.
static uint16_t chk_view_index(int32_t at_ms, uint8_t cadence) {
  if (cadence == 0 || at_ms <= 0) {
    return 0;
  }
  return (uint16_t)(at_ms / 1000 / cadence);
}

// the marks in the check's slot order, as indices; an unset slot reads as zero
// and the encoder folds it into the mark before it
static void chk_view_marks(const int32_t *marks, size_t num, uint8_t cadence, chk_view_t *v) {
  for (size_t i = 0; i < num && i < CHK_MARKS; i++) {
    v->marks[i] = chk_view_index(marks[i], cadence);
  }
  v->num_marks = num;
}

/* Ventilation */

// The outdoor floor and the baseline both wait for the reading to settle
// rather than for a count: outside, the sensor falls from room air to outdoor
// air; back inside, it climbs from outdoor air to the room's. Either takes
// the sensor about three minutes to make from a step of a few hundred ppm,
// so the floor is two minutes and the cap five, after which the reading is
// taken as it stands.
#define CHK_VENT_SETTLE_MIN_MS 120000
#define CHK_VENT_SETTLE_MAX_MS 300000

// Where the flow can be resumed from after a deep sleep, which on this device
// is a reset: main memory is gone and the flow is re-entered from the top, so
// each step records that it is done before moving on.
enum {
  VENT_STEP_INTRO,
  VENT_STEP_ROOM,
  VENT_STEP_OUTDOOR,
  VENT_STEP_OUTSIDE,
  VENT_STEP_PRECHECK,
  VENT_STEP_BASELINE,
  VENT_STEP_TRIGGER,
  VENT_STEP_MEASURE,
  VENT_STEP_RESULT,
};

// the two ways to come by the outdoor floor, in the order the list shows them
enum {
  VENT_OUTDOOR_USE,
  VENT_OUTDOOR_MEASURE,
};

// The measurement runs at least ninety seconds so the fit has a span, at most
// five, and stops early once a fifth of the excess has gone.
#define CHK_VENT_MIN_MS 90000
#define CHK_VENT_MAX_MS 300000
#define CHK_VENT_SAMPLES 60
#define CHK_VENT_EARLY 0.2f

// Ask after two minutes of nothing: an open window shows within that.
#define CHK_VENT_NUDGE_MS 120000

// Outdoor CO2 is an input rather than a constant, because urban air reaches
// 600 ppm and a 50 ppm error moves the estimate by about a third. It is
// measured outside or assumed, never typed in, since nobody knows today's
// value; and the precheck wants this much excess above it before there is
// anything to measure.
#define CHK_VENT_EXCESS_MIN 200.0f

// The age of a remembered floor, said out loud: the hour it was measured in
// is "just now", then hours, then days.
static const char *chk_vent_age_text(float hours) {
  if (hours < 1) {
    return CHK_TEXT(vent__age_now);
  } else if (hours < 48) {
    return lvx_fmt(CHK_TEXT(vent__age_hours), (int)hours);
  }
  return lvx_fmt(CHK_TEXT(vent__age_days), (int)(hours / 24));
}

static chk_step_t vent_decay_sample(chk_t *c, float value, int32_t t_ms) {
  // feed the fit
  chk_vent_observe(c, value, t_ms);

  // enough of the excess gone to call it measured
  float excess0 = c->result[CHK_VENT_C0] - c->result[CHK_VENT_COUT];
  float gone = c->result[CHK_VENT_C0] - value;
  if (excess0 > 0 && gone >= CHK_VENT_EARLY * excess0) {
    return CHK_STEP_DONE;
  }

  // something is moving, so stop asking about the window
  if (gone >= 10.0f) {
    return CHK_STEP_GO;
  }

  return CHK_STEP_WAIT;
}

// the run ends on the share above, so the screen counts down to the same one
static int32_t vent_decay_remaining(chk_t *c) {
  return chk_vent_remaining(c, CHK_VENT_EARLY);
}

static void chk_view_vent(const float *r, const int32_t *marks, uint8_t cadence, chk_view_t *v) {
  v->title = CHK_TEXT(vent__title);
  v->check = CHK_CODE_VENT;
  v->signal = AL_SAMPLE_CO2;
  v->note = CHK_TEXT(vent__stat_note);

  // the half-life and the time to fresh follow from the rate
  int half = chk_round_minutes(chk_half_life(r[CHK_VENT_ACH]));
  v->lines[0] = lvx_fmt(CHK_TEXT(vent__stat_ach), r[CHK_VENT_ACH], r[CHK_VENT_ACH_BAND]);
  v->lines[1] = lvx_fmt(CHK_TEXT(vent__stat_half_life), half);
  v->lines[2] = lvx_fmt(CHK_TEXT(vent__stat_fresh), chk_round_minutes(chk_fresh_time(r[CHK_VENT_ACH])));
  v->lines[3] = lvx_fmt(CHK_TEXT(vent__stat_co2), r[CHK_VENT_C0], r[CHK_VENT_CLAST], r[CHK_VENT_COUT]);
  v->num_lines = 4;

  // ach, achSe, r2, c0, c1, cout, coutHow, coutAge
  v->payload[0] = r[CHK_VENT_ACH];
  v->payload[1] = r[CHK_VENT_ACH_BAND];
  v->payload[2] = r[CHK_VENT_R2];
  v->payload[3] = r[CHK_VENT_C0];
  v->payload[4] = r[CHK_VENT_CLAST];
  v->payload[5] = r[CHK_VENT_COUT];
  // where the floor came from, so the page can say so: the estimate's
  // accuracy rests on it
  v->payload[6] = r[CHK_VENT_COUT_HOW];
  v->payload[7] = r[CHK_VENT_COUT_AGE];
  v->num_payload = 8;
  // where the baseline held, the window went open and the decay stopped,
  // which is where the page bands the chart
  chk_view_marks(marks, CHK_VENT_MARKS, cadence, v);

  // the one number the verdict is about
  // the verdict: the one number, and the advice the tier earns
  chk_vent_tier_t tier = chk_vent_tier(r[CHK_VENT_ACH]);
  const char *advice = tier == CHK_VENT_TIER_LOW   ? CHK_TEXT(vent__advice_low)
                       : tier == CHK_VENT_TIER_MID ? CHK_TEXT(vent__advice_mid)
                                                   : CHK_TEXT(vent__advice_high);
  v->verdict[0] = (chk_bubble_t){tier == CHK_VENT_TIER_HIGH ? &img_robin_happy : &img_robin_standing,
                                 lvx_fmt(CHK_TEXT(vent__verdict_half_life), half), CHK_TEXT(next)};
  v->verdict[1] = (chk_bubble_t){&img_robin_pointing, advice, CHK_TEXT(next)};
  v->num_verdict = 2;
}

void *chk_vent_run(void *on_exit, void *on_idle, void *self) {
  // pick the check up where it stopped, or start it
  chk_t *c = chk_context();
  if (!chk_resuming(c, CHK_VENT)) {
    chk_begin(c, CHK_VENT);
  }
  chk_park_into(self);

  const char *title = CHK_TEXT(vent__title);

  // The steps run in order and fall through into one another; the B key sets
  // the step back and restarts the loop, and a step entered backwards opens on
  // its last screen rather than its first.
  bool back = false;
  for (;;) {
    /* Introduction */

    if (c->step == VENT_STEP_INTRO) {
      const chk_bubble_t intro[] = {
          {&img_robin_happy, CHK_TEXT(vent__intro_1), CHK_TEXT(next)},
          {&img_robin_pointing, CHK_TEXT(vent__intro_2), CHK_TEXT(next)},
          {&img_robin_pointing, CHK_TEXT(vent__intro_3), CHK_TEXT(next)},
          {&img_robin_standing, CHK_TEXT(vent__intro_4), CHK_TEXT(start)},
      };
      size_t n = sizeof(intro) / sizeof(intro[0]);
      CHK_TRY(chk_say_from(intro, n, back ? n - 1 : 0), CHK_LEAVE);
      c->step = VENT_STEP_ROOM;
    }

    /* Room */

    // where the check runs, for the record and the page: the registry rows in
    // their order, likeliest first, opening on the room of the last check. The
    // last row leaves it out, and the page then says nothing about the room
    if (c->step == VENT_STEP_ROOM) {
      const chk_bubble_t ask = {&img_robin_pointing, CHK_TEXT(vent__room_ask), CHK_TEXT(next)};
      CHK_TRY(chk_say(&ask, 1), CHK_STEP(VENT_STEP_INTRO));

      // row i is room i + 1, and the row after the last room is none
      const char *rows[] = {
          CHK_TEXT(room__living),
          CHK_TEXT(room__bedroom),
          CHK_TEXT(room__kitchen),
          CHK_TEXT(room__office),
          CHK_TEXT(room__bathroom),
          CHK_TEXT(room__kids),
          CHK_TEXT(room__meeting),
          CHK_TEXT(room__classroom),
          CHK_TEXT(room__hallway),
          CHK_TEXT(room__basement),
          CHK_TEXT(room__workshop),
          CHK_TEXT(room__garage),
          CHK_TEXT(room__car),
          CHK_TEXT(room__hotel),
          CHK_TEXT(room__outdoors),
          CHK_TEXT(room__none),
          NULL,
      };
      int none = (int)(sizeof(rows) / sizeof(rows[0])) - 2;
      uint8_t last = chk_room_last();
      int start = last == CHK_CODE_ROOM_NONE ? none : last - 1;

      // as with the floor, leaving and timing out both go back to the bubble
      int offset = 0;
      int chosen = gui_list_strings(start, &offset, rows, CHK_TEXT(next), CHK_TEXT(back), GUI_INACTION);
      if (chosen < 0) {
        continue;
      }
      c->room = chosen == none ? CHK_CODE_ROOM_NONE : (uint8_t)(chosen + 1);
      chk_room_remember(c->room);
      c->step = VENT_STEP_OUTDOOR;
    }

    /* Outdoor floor */

    // the floor the decay falls towards: the remembered value, or a walk
    // outside to measure it. The list's row says what "use" would use, so the
    // assumption is on the screen the user chooses from.
    if (c->step == VENT_STEP_OUTDOOR) {
      const chk_bubble_t ask = {&img_robin_pointing, CHK_TEXT(vent__outdoor_ask), CHK_TEXT(next)};
      CHK_TRY(chk_say(&ask, 1), CHK_STEP(VENT_STEP_ROOM));

      float ppm, age;
      chk_vent_cout_how_t how = chk_vent_outdoor_get(al_clock_get_epoch(), &ppm, &age);
      const char *use = how == CHK_VENT_COUT_ASSUMED
                            ? lvx_fmt(CHK_TEXT(vent__outdoor_default), ppm)
                            : lvx_fmt(CHK_TEXT(vent__outdoor_remembered), ppm, chk_vent_age_text(age));
      const char *rows[] = {use, CHK_TEXT(vent__outdoor_measure), NULL};

      // the list cannot tell leaving from timing out, so both go back to the
      // bubble, which times out on its own if nobody is there
      int offset = 0;
      int chosen = gui_list_strings(0, &offset, rows, CHK_TEXT(next), CHK_TEXT(back), GUI_INACTION);
      if (chosen < 0) {
        continue;
      }
      if (chosen == VENT_OUTDOOR_MEASURE) {
        c->step = VENT_STEP_OUTSIDE;
      } else {
        c->result[CHK_VENT_COUT] = ppm;
        c->result[CHK_VENT_COUT_HOW] = (float)how;
        c->result[CHK_VENT_COUT_AGE] = age;
        c->step = VENT_STEP_PRECHECK;
      }
    }

    /* Outside */

    if (c->step == VENT_STEP_OUTSIDE) {
      const chk_bubble_t go = {&img_robin_pointing, CHK_TEXT(vent__go_outside), CHK_TEXT(start)};
      CHK_TRY(chk_say(&go, 1), CHK_STEP(VENT_STEP_OUTDOOR));

      // the reading falls from room air to outdoor air and is taken once it
      // holds; the key drops the run and asks again
      const chk_screen_t outside = {
          .title = title,
          .stage = CHK_TEXT(stage__outside),
          .hint = CHK_TEXT(vent__outside_hint),
          .unit = "ppm",
          .show = CHK_SHOW_PROGRESS,
          .back = CHK_TEXT(back),
          .field = AL_SAMPLE_CO2,
          .cfg = {.min_ms = CHK_VENT_SETTLE_MIN_MS, .max_ms = CHK_VENT_SETTLE_MAX_MS, .settle = true},
      };
      CHK_TRY(chk_measure(c, &outside), (chk_measure_reset(&c->run), CHK_STEP(VENT_STEP_OUTSIDE)));

      float ppm = chk_settle_value(&c->run);
      bool rough = !chk_settle_done(&c->run);

      // a floor that reads like indoor air is doubted, not refused: the key
      // goes back out to measure again, the action takes it as it is
      if (ppm > CHK_VENT_OUTDOOR_MAX) {
        const chk_bubble_t doubt = {&img_robin_standing, lvx_fmt(CHK_TEXT(vent__outdoor_doubt), ppm),
                                    CHK_TEXT(vent__outdoor_anyway)};
        CHK_TRY(chk_say(&doubt, 1), (chk_measure_reset(&c->run), CHK_STEP(VENT_STEP_OUTSIDE)));
      }

      // the baseline is a run of its own: left as it is, the outdoor run's
      // clock and settle state would carry into it
      chk_measure_reset(&c->run);

      chk_vent_outdoor_set(ppm, rough, al_clock_get_epoch());
      c->result[CHK_VENT_COUT] = ppm;
      c->result[CHK_VENT_COUT_HOW] = (float)(rough ? CHK_VENT_COUT_ROUGH : CHK_VENT_COUT_MEASURED);
      c->result[CHK_VENT_COUT_AGE] = 0;

      c->step = VENT_STEP_PRECHECK;

      const chk_bubble_t got = {&img_robin_happy, lvx_fmt(CHK_TEXT(vent__outdoor_got), ppm), CHK_TEXT(next)};
      CHK_TRY(chk_say(&got, 1), CHK_STEP(VENT_STEP_OUTSIDE));
    }

    /* Precheck */

    // the single-zone assumption the decay rests on is the user's to meet
    if (c->step == VENT_STEP_PRECHECK) {
      const char *const items[] = {
          CHK_TEXT(vent__list_windows),
          CHK_TEXT(vent__list_door),
          CHK_TEXT(vent__list_table),
      };
      CHK_TRY(chk_list(title, CHK_TEXT(stage__baseline), items, 3, back), CHK_STEP(VENT_STEP_OUTDOOR));
      c->step = VENT_STEP_BASELINE;
    }

    /* Baseline */

    // the reading climbs back from outdoor air, or has sat in the room all
    // along, and is taken once it holds: a room with someone in it never
    // reaches a plateau, so settled means the sensor has caught up, not the air
    if (c->step == VENT_STEP_BASELINE) {
      // the series starts here: the intro, the checklist and the walk outside
      // are not part of the check's own record, so the clock and the record
      // start afresh, unless this is a resume into a baseline under way
      if (c->run.began == 0) {
        chk_restart(c);
      }
      const chk_screen_t baseline = {
          .title = title,
          .stage = CHK_TEXT(stage__baseline),
          .hint = CHK_TEXT(vent__baseline_hint),
          .unit = "ppm",
          .show = CHK_SHOW_PROGRESS,
          .back = CHK_TEXT(back),
          .field = AL_SAMPLE_CO2,
          .cfg = {.min_ms = CHK_VENT_SETTLE_MIN_MS, .max_ms = CHK_VENT_SETTLE_MAX_MS, .settle = true},
      };
      CHK_TRY(chk_measure(c, &baseline), CHK_REDO(VENT_STEP_PRECHECK));
      chk_mark(c, CHK_VENT_MARK_SETTLED);

      // the baseline is the newest settled window, or what the store holds
      // when the run ended before one
      c->result[CHK_VENT_C0] = chk_settle_value(&c->run);
      if (isnan(c->result[CHK_VENT_C0])) {
        c->result[CHK_VENT_C0] = chk_vent_baseline_median(CHK_SETTLE_WINDOW);
      }

      // there has to be something above outdoor to watch leave
      if (c->result[CHK_VENT_C0] - c->result[CHK_VENT_COUT] < CHK_VENT_EXCESS_MIN) {
        const chk_bubble_t fresh = {&img_robin_standing, CHK_TEXT(vent__already_fresh), CHK_TEXT(ok)};
        return chk_leave(c, chk_say(&fresh, 1), on_exit, on_idle, NULL);
      }

      // the decay is a run of its own, not a continuation of the baseline
      chk_measure_reset(&c->run);
      c->step = VENT_STEP_TRIGGER;
    }

    /* Trigger */

    if (c->step == VENT_STEP_TRIGGER) {
      // cue the user, unless this is a timer wake re-entering the prompt they
      // left on the table
      if (scr_awake()) {
        al_buzzer_beep(1047, 80, false);
      }
      const chk_bubble_t open = {
          &img_robin_pointing,
          lvx_fmt(CHK_TEXT(vent__open_window), c->result[CHK_VENT_C0]),
          CHK_TEXT(vent__window_is_open),
      };
      CHK_TRY(chk_say(&open, 1), CHK_REDO(VENT_STEP_BASELINE));

      // the window goes open now: the decay starts here, and the stretch since
      // the baseline held was the user at the device
      chk_mark(c, CHK_VENT_MARK_OPENED);
      c->step = VENT_STEP_MEASURE;
    }

    /* Measurement */

    // the window is open, so there is no going back from here, only stopping
    if (c->step == VENT_STEP_MEASURE) {
      const chk_screen_t decay = {
          .title = title,
          .stage = CHK_TEXT(stage__measuring),
          .nudge = CHK_TEXT(vent__nudge),
          .unit = "ppm",
          .show = CHK_SHOW_CHART,
          .field = AL_SAMPLE_CO2,
          .cfg = {.min_ms = CHK_VENT_MIN_MS,
                  .max_ms = CHK_VENT_MAX_MS,
                  .capacity = CHK_VENT_SAMPLES,
                  .nudge_ms = CHK_VENT_NUDGE_MS},
          .on_sample = vent_decay_sample,
          .on_remaining = vent_decay_remaining,
          .floor = c->result[CHK_VENT_COUT],
      };
      CHK_TRY(chk_measure(c, &decay), CHK_LEAVE);
      chk_mark(c, CHK_VENT_MARK_STOPPED);
      c->step = VENT_STEP_RESULT;
      c->phase = RESULT_PHASE_VERDICT;
    }

    /* Result */

    chk_vent_quality_t quality = chk_vent_evaluate(c, c->run.elapsed);

    // no number worth reporting: say which way the air went and offer
    // another go
    if (quality != CHK_VENT_SOLID) {
      const char *direction = quality == CHK_VENT_QUICK ? CHK_TEXT(vent__quickly) : CHK_TEXT(vent__slowly);
      const chk_bubble_t unclear = {
          &img_robin_standing,
          lvx_fmt(CHK_TEXT(vent__unclear), direction),
          CHK_TEXT(again),
      };
      return chk_leave(c, chk_say(&unclear, 1), on_exit, on_idle, self);
    }

    // seal it before saying anything: walking away from the verdict should
    // not lose the check. A result step re-entered afterwards gets the same
    // record.
    uint16_t stored = chk_record(c, AL_SAMPLE_CO2);

    /* Verdict, stats and share */

    return chk_finish(c, CHK_VENT, stored, on_exit, on_idle, self);
  }
}

/* Gas stove */

// Three passes with a pot of water; the only structural difference from the
// ventilation check is that the middle of it is a loop.

#define CHK_STOVE_BASELINE_N 12

// Where the flow resumes after a deep sleep. The passes share one step and
// the pass index is the context's phase, so a sleep inside pass two comes
// back into pass two.
enum {
  STOVE_STEP_INTRO,
  STOVE_STEP_PRECHECK,
  STOVE_STEP_BASELINE,
  STOVE_STEP_PASSES,
  STOVE_STEP_RESULT,
};

// what each pass asks for and how it ends
typedef struct {
  const char *prompt;
  const char *action;
  const char *stage;
  bool burning;
} chk_stove_pass_t;

static chk_step_t stove_sample(chk_t *c, float value, int32_t t_ms) {
  // the clearing pass falls, the burn passes rise
  if (c->phase == CHK_STOVE_PASS_CLEAR) {
    chk_stove_observe_clear(c, value, t_ms);
    float gone = c->result[CHK_STOVE_PEAK] - value;
    if (gone >= 0.5f * (c->result[CHK_STOVE_PEAK] - c->result[CHK_STOVE_C0])) {
      return CHK_STEP_DONE;
    }
    return gone >= 10.0f ? CHK_STEP_GO : CHK_STEP_WAIT;
  }

  chk_stove_observe_burn(c, c->phase, value, t_ms);

  // stop the experiment once the kitchen has had enough, whatever the fit
  if (value >= CHK_STOVE_ABORT_PPM) {
    return CHK_STEP_DONE;
  }

  // enough signal for a slope
  float risen = value - c->result[CHK_STOVE_C0];
  if (risen >= CHK_STOVE_BURN_RISE) {
    return CHK_STEP_DONE;
  }

  return risen >= 10.0f ? CHK_STEP_GO : CHK_STEP_WAIT;
}

static void chk_view_stove(const float *r, const int32_t *marks, uint8_t cadence, chk_view_t *v) {
  v->title = CHK_TEXT(stove__title);
  v->check = CHK_CODE_STOVE;
  v->signal = AL_SAMPLE_CO2;
  v->note = CHK_TEXT(stove__stat_note);

  int percent = (int)(r[CHK_STOVE_CAPTURE] * 100 + 0.5f);
  v->lines[0] = lvx_fmt(CHK_TEXT(stove__stat_capture), percent);
  v->lines[1] = r[CHK_STOVE_HOOD_ACH] > 0 ? lvx_fmt(CHK_TEXT(stove__stat_hood), r[CHK_STOVE_HOOD_ACH])
                                          : CHK_TEXT(stove__stat_hood_none);
  v->lines[2] = lvx_fmt(CHK_TEXT(stove__stat_peak), r[CHK_STOVE_PEAK]);
  v->num_lines = 3;

  // ce, hoodAch, slope1, slope3, c0, noxPeak
  v->payload[0] = (float)percent;
  v->payload[1] = r[CHK_STOVE_HOOD_ACH] > 0 ? r[CHK_STOVE_HOOD_ACH] : 0;
  v->payload[2] = r[CHK_STOVE_SLOPE1];
  v->payload[3] = r[CHK_STOVE_SLOPE3];
  v->payload[4] = r[CHK_STOVE_C0];
  v->payload[5] = r[CHK_STOVE_NOX];
  v->num_payload = 6;
  // the baseline taken, then each pass's prompt answered and run ended,
  // which is where the page bands the chart
  chk_view_marks(marks, CHK_STOVE_MARKS, cadence, v);

  // the verdict: the share caught, and the advice the tier earns
  chk_stove_tier_t tier = chk_stove_tier(r[CHK_STOVE_CAPTURE]);
  const char *advice = tier == CHK_STOVE_TIER_LOW   ? CHK_TEXT(stove__advice_low)
                       : tier == CHK_STOVE_TIER_MID ? CHK_TEXT(stove__advice_mid)
                                                    : CHK_TEXT(stove__advice_high);
  v->verdict[0] = (chk_bubble_t){tier == CHK_STOVE_TIER_HIGH ? &img_robin_happy : &img_robin_standing,
                                 lvx_fmt(CHK_TEXT(stove__verdict), percent), CHK_TEXT(next)};
  v->verdict[1] = (chk_bubble_t){&img_robin_pointing, advice, CHK_TEXT(next)};
  v->num_verdict = 2;
}

void *chk_stove_run(void *on_exit, void *on_idle, void *self) {
  // pick the check up where it stopped, or start it
  chk_t *c = chk_context();
  if (!chk_resuming(c, CHK_STOVE)) {
    chk_begin(c, CHK_STOVE);
    // a stove check runs in the kitchen by definition, so it is not asked
    c->room = CHK_CODE_ROOM_KITCHEN;
  }
  chk_park_into(self);

  const char *title = CHK_TEXT(stove__title);

  const chk_stove_pass_t passes[] = {
      {CHK_TEXT(stove__pass_open), CHK_TEXT(stove__burner_on), CHK_TEXT(stove__stage_open), true},
      {CHK_TEXT(stove__pass_clear), CHK_TEXT(stove__hood_on), CHK_TEXT(stove__stage_clear), false},
      {CHK_TEXT(stove__pass_hood), CHK_TEXT(stove__burner_on), CHK_TEXT(stove__stage_hood), true},
  };

  // the step loop, as the ventilation check has it
  bool back = false;
  for (;;) {
    /* Introduction */

    if (c->step == STOVE_STEP_INTRO) {
      const chk_bubble_t intro[] = {
          {&img_robin_happy, CHK_TEXT(stove__intro_1), CHK_TEXT(next)},
          {&img_robin_pointing, CHK_TEXT(stove__intro_2), CHK_TEXT(next)},
          {&img_robin_standing, CHK_TEXT(stove__intro_3), CHK_TEXT(start)},
      };
      size_t n = sizeof(intro) / sizeof(intro[0]);
      CHK_TRY(chk_say_from(intro, n, back ? n - 1 : 0), CHK_LEAVE);
      c->step = STOVE_STEP_PRECHECK;
    }

    /* Precheck */

    if (c->step == STOVE_STEP_PRECHECK) {
      const char *const items[] = {
          CHK_TEXT(stove__list_off),
          CHK_TEXT(stove__list_pot),
          CHK_TEXT(stove__list_away),
      };
      CHK_TRY(chk_list(title, CHK_TEXT(stage__baseline), items, 3, back), CHK_STEP(STOVE_STEP_INTRO));
      c->step = STOVE_STEP_BASELINE;
    }

    /* Baseline */

    if (c->step == STOVE_STEP_BASELINE) {
      // the series starts here, as in the ventilation check
      if (c->run.began == 0) {
        chk_restart(c);
      }
      const chk_screen_t baseline = {
          .title = title,
          .stage = CHK_TEXT(stage__baseline),
          .hint = CHK_TEXT(stove__baseline_hint),
          .unit = "ppm",
          .show = CHK_SHOW_PROGRESS,
          .back = CHK_TEXT(back),
          .field = AL_SAMPLE_CO2,
          .cfg = {.capacity = CHK_STOVE_BASELINE_N},
          .on_sample = chk_baseline_sample,
      };
      CHK_TRY(chk_measure(c, &baseline), CHK_REDO(STOVE_STEP_PRECHECK));
      chk_mark(c, CHK_STOVE_MARK_SETTLED);

      c->result[CHK_STOVE_C0] = chk_vent_baseline_median(CHK_STOVE_BASELINE_N);
      c->result[CHK_STOVE_PEAK] = c->result[CHK_STOVE_C0];

      // the first pass is a run of its own
      chk_measure_reset(&c->run);
      c->phase = 0;
      c->step = STOVE_STEP_PASSES;
    }

    /* Three passes */

    // one pass per trip round the loop, the pass index being the context's
    // phase, so a sleep inside pass two comes back into pass two
    if (c->step == STOVE_STEP_PASSES) {
      const chk_stove_pass_t *pass = &passes[c->phase];

      // the prompt, unless the pass is already under way: a resume into a
      // measurement must not ask for the burner again. Before the first pass
      // the key goes back to the baseline; once a burner has been on there
      // is no going back, only stopping.
      if (c->run.began == 0) {
        // cue the user, unless this is a timer wake re-entering the prompt
        // they left on the table
        if (scr_awake()) {
          al_buzzer_beep(1047, 80, false);
        }
        const chk_bubble_t prompt = {&img_robin_pointing, pass->prompt, pass->action};
        CHK_TRY(chk_say(&prompt, 1), c->phase == 0 ? CHK_REDO(STOVE_STEP_BASELINE) : CHK_LEAVE);

        // the pass starts now: the burner or the hood goes on, and the
        // stretch since the last run ended was the user at the device
        chk_mark(c, (uint8_t)(CHK_STOVE_MARK_PASS1_ON + 2 * c->phase));
      }

      // a burn is drawn against the rise that ends it, the clearing against
      // the excess it has to lose
      const chk_screen_t measure = {
          .title = title,
          .stage = pass->stage,
          .nudge = pass->burning ? CHK_TEXT(stove__nudge) : NULL,
          .unit = "ppm",
          .show = CHK_SHOW_CHART,
          .field = AL_SAMPLE_CO2,
          .cfg = {.min_ms = CHK_STOVE_SLOPE_MS, .max_ms = CHK_STOVE_PASS_MAX_MS, .nudge_ms = CHK_STOVE_SLOPE_MS},
          .on_sample = stove_sample,
          .floor = c->result[CHK_STOVE_C0],
          .range = pass->burning ? CHK_STOVE_BURN_RISE : c->result[CHK_STOVE_PEAK] - c->result[CHK_STOVE_C0],
      };
      CHK_TRY(chk_measure(c, &measure), CHK_LEAVE);

      // where this pass ended, so the page can band the chart per pass
      chk_mark(c, (uint8_t)(CHK_STOVE_MARK_PASS1_DONE + 2 * c->phase));

      // the kitchen has had enough: stop the check rather than the pass
      if (c->result[CHK_STOVE_PEAK] >= CHK_STOVE_ABORT_PPM) {
        const chk_bubble_t stop = {&img_robin_angry1, CHK_TEXT(stove__too_much), CHK_TEXT(ok)};
        return chk_leave(c, chk_say(&stop, 1), on_exit, on_idle, NULL);
      }

      // the next pass starts fresh
      chk_measure_reset(&c->run);
      if (++c->phase < 3) {
        continue;
      }
      c->step = STOVE_STEP_RESULT;
      c->phase = RESULT_PHASE_VERDICT;
    }

    /* Result */

    if (chk_stove_evaluate(c) != CHK_STOVE_SOLID) {
      const chk_bubble_t unclear[] = {
          {&img_robin_standing, CHK_TEXT(stove__unclear_1), CHK_TEXT(next)},
          {&img_robin_standing, CHK_TEXT(stove__unclear_2), CHK_TEXT(again)},
      };
      return chk_leave(c, chk_say(unclear, 2), on_exit, on_idle, self);
    }

    // seal it before saying anything, as the ventilation check does
    uint16_t stored = chk_record(c, AL_SAMPLE_CO2);

    /* Verdict, stats and share */

    return chk_finish(c, CHK_STOVE, stored, on_exit, on_idle, self);
  }
}

/* Bedroom night */

// One night, from a baseline before lying down to a key in the morning. The
// structural difference from the checks above is the length: the run has no
// natural end, so the user ends it, and at ten hours neither the short store
// nor the panel can be treated the way a five-minute run treats them.

// The record and the payload run at two minutes, so a ten-hour night is 300
// samples, well under the 512 the format holds. The device keeps sampling at
// its own cadence and every reading is folded into the statistics; only the
// curve is thinned.
#define CHK_BEDROOM_RECORD_S 120

// the baseline waits for the reading to hold, as the ventilation check's does
#define CHK_BEDROOM_SETTLE_MIN_MS 120000
#define CHK_BEDROOM_SETTLE_MAX_MS 300000

// The night: no signal ends it, so the key does. Under half an hour the key
// is taken as a slip and the flow asks; at fourteen hours the run stops on
// its own, which is longer than a night and shorter than a forgotten device.
#define CHK_BEDROOM_MIN_MS (30 * 60 * 1000)
#define CHK_BEDROOM_MAX_MS (14 * 60 * 60 * 1000)

// Ten-minute slots draw eleven hours across the seventy the chart has, and
// the panel is redrawn every five minutes rather than every reading.
#define CHK_BEDROOM_SLOT_MS (10 * 60 * 1000)
#define CHK_BEDROOM_REDRAW_MS (5 * 60 * 1000)

// Where the flow can be resumed from after a deep sleep.
enum {
  BEDROOM_STEP_INTRO,
  BEDROOM_STEP_SETUP,
  BEDROOM_STEP_SLEEPERS,
  BEDROOM_STEP_PRECHECK,
  BEDROOM_STEP_BASELINE,
  BEDROOM_STEP_TRIGGER,
  BEDROOM_STEP_NIGHT,
  BEDROOM_STEP_RESULT,
};

static chk_step_t bedroom_night_sample(chk_t *c, float value, int32_t t_ms) {
  // the temperature and the humidity ride in the same sample as the CO2 and
  // are folded in as the bands the night stayed in, not as series of their
  // own: the payload carries one series, and a night has no room for three
  al_sample_t sample = al_store_last();
  float tmp = NAN, rh = NAN;
  if (al_sample_valid(sample)) {
    tmp = al_sample_read(sample, AL_SAMPLE_TMP);
    rh = al_sample_read(sample, AL_SAMPLE_HUM);
  }
  chk_bedroom_observe(c, value, tmp, rh, t_ms);

  // nothing the air does ends the night; the morning does
  return CHK_STEP_WAIT;
}

static void chk_view_bedroom(const float *r, const int32_t *marks, uint8_t cadence, chk_view_t *v) {
  v->title = CHK_TEXT(bedroom__title);
  v->check = CHK_CODE_BEDROOM;
  v->signal = AL_SAMPLE_CO2;
  v->note = CHK_TEXT(bedroom__stat_note);

  v->lines[0] = lvx_fmt(CHK_TEXT(bedroom__stat_peak), r[CHK_BEDROOM_CMAX]);
  v->lines[1] = lvx_fmt(CHK_TEXT(bedroom__stat_mean), r[CHK_BEDROOM_CMEAN]);
  v->lines[2] = lvx_fmt(CHK_TEXT(bedroom__stat_over), r[CHK_BEDROOM_OVER_LOW]);
  v->lines[3] = lvx_fmt(CHK_TEXT(bedroom__stat_temp), r[CHK_BEDROOM_TMIN], r[CHK_BEDROOM_TMAX]);
  v->lines[4] = lvx_fmt(CHK_TEXT(bedroom__stat_hum), r[CHK_BEDROOM_RHMIN], r[CHK_BEDROOM_RHMAX]);
  // a night too short to have settled reports no flow rather than a number
  // the room never reached
  v->lines[5] = r[CHK_BEDROOM_FLOW] > 0 ? lvx_fmt(CHK_TEXT(bedroom__stat_flow), r[CHK_BEDROOM_FLOW])
                                        : CHK_TEXT(bedroom__stat_flow_none);
  v->num_lines = 6;

  // c0, cMax, cMean, c1, hoursOver1150, hoursOver2600, tMin, tMax, rhMin,
  // rhMax, sleepers, setup, flow, plateau
  v->payload[0] = r[CHK_BEDROOM_C0];
  v->payload[1] = r[CHK_BEDROOM_CMAX];
  v->payload[2] = r[CHK_BEDROOM_CMEAN];
  v->payload[3] = r[CHK_BEDROOM_C1];
  v->payload[4] = r[CHK_BEDROOM_OVER_LOW];
  v->payload[5] = r[CHK_BEDROOM_OVER_HIGH];
  v->payload[6] = r[CHK_BEDROOM_TMIN];
  v->payload[7] = r[CHK_BEDROOM_TMAX];
  v->payload[8] = r[CHK_BEDROOM_RHMIN];
  v->payload[9] = r[CHK_BEDROOM_RHMAX];
  v->payload[10] = r[CHK_BEDROOM_SLEEPERS];
  v->payload[11] = r[CHK_BEDROOM_SETUP];
  // the flow, and whether the room had settled by morning: without the
  // plateau it is a lower bound, and the page says so
  v->payload[12] = r[CHK_BEDROOM_FLOW];
  v->payload[13] = r[CHK_BEDROOM_PLATEAU];
  v->num_payload = 14;
  // where the baseline held, the sleepers lay down and the morning came
  chk_view_marks(marks, CHK_BEDROOM_MARKS, cadence, v);

  // the verdict: the night's mean with the peak beside it, then the advice
  // the tier earns. The scale is graded on the mean, which is what the sleep
  // studies state their bands on; the peak is a statistic next to it
  chk_bedroom_tier_t tier = chk_bedroom_tier(r[CHK_BEDROOM_CMEAN]);
  const char *advice = tier == CHK_BEDROOM_TIER_LOW   ? CHK_TEXT(bedroom__advice_low)
                       : tier == CHK_BEDROOM_TIER_MID ? CHK_TEXT(bedroom__advice_mid)
                                                      : CHK_TEXT(bedroom__advice_high);
  const char *said = lvx_fmt(CHK_TEXT(bedroom__verdict), r[CHK_BEDROOM_CMEAN], r[CHK_BEDROOM_CMAX]);
  v->verdict[0] =
      (chk_bubble_t){tier == CHK_BEDROOM_TIER_HIGH ? &img_robin_happy : &img_robin_standing, said, CHK_TEXT(next)};
  v->verdict[1] = (chk_bubble_t){&img_robin_pointing, advice, CHK_TEXT(next)};
  v->num_verdict = 2;
}

void *chk_bedroom_run(void *on_exit, void *on_idle, void *self) {
  // pick the check up where it stopped, or start it
  chk_t *c = chk_context();
  if (!chk_resuming(c, CHK_BEDROOM)) {
    chk_begin(c, CHK_BEDROOM);
    // a bedroom check runs in the bedroom by definition, so it is not asked
    c->room = CHK_CODE_ROOM_BEDROOM;
  }
  // the record runs slower than the sensor, which is what keeps a night
  // inside the sample cap
  c->record = CHK_BEDROOM_RECORD_S;
  chk_park_into(self);

  const char *title = CHK_TEXT(bedroom__title);

  // the step loop, as the checks above have it
  bool back = false;
  for (;;) {
    /* Introduction */

    if (c->step == BEDROOM_STEP_INTRO) {
      const chk_bubble_t intro[] = {
          {&img_robin_happy, CHK_TEXT(bedroom__intro_1), CHK_TEXT(next)},
          {&img_robin_pointing, CHK_TEXT(bedroom__intro_2), CHK_TEXT(next)},
          {&img_robin_pointing, CHK_TEXT(bedroom__intro_3), CHK_TEXT(next)},
          {&img_robin_standing, CHK_TEXT(bedroom__intro_4), CHK_TEXT(start)},
      };
      size_t n = sizeof(intro) / sizeof(intro[0]);
      CHK_TRY(chk_say_from(intro, n, back ? n - 1 : 0), CHK_LEAVE);
      c->step = BEDROOM_STEP_SETUP;
    }

    /* Setup */

    // how the room is set up tonight, which is the thing the night is about:
    // the same room on two nights is two results the page can compare
    if (c->step == BEDROOM_STEP_SETUP) {
      const chk_bubble_t ask = {&img_robin_pointing, CHK_TEXT(bedroom__setup_ask), CHK_TEXT(next)};
      CHK_TRY(chk_say(&ask, 1), CHK_STEP(BEDROOM_STEP_INTRO));

      // row i is setup i + 1
      const char *rows[] = {
          CHK_TEXT(bedroom__setup_closed),
          CHK_TEXT(bedroom__setup_tilted),
          CHK_TEXT(bedroom__setup_open),
          CHK_TEXT(bedroom__setup_door),
          NULL,
      };

      // as with the room picker, leaving and timing out both go back to the
      // bubble, which times out on its own if nobody is there
      int offset = 0;
      int chosen = gui_list_strings((int)c->result[CHK_BEDROOM_SETUP] - 1, &offset, rows, CHK_TEXT(next),
                                    CHK_TEXT(back), GUI_INACTION);
      if (chosen < 0) {
        continue;
      }
      c->result[CHK_BEDROOM_SETUP] = (float)(chosen + 1);
      c->step = BEDROOM_STEP_SLEEPERS;
    }

    /* Sleepers */

    // the flow is per sleeper whatever the count, but the count is what makes
    // it a room's worth of air on the page
    if (c->step == BEDROOM_STEP_SLEEPERS) {
      const chk_bubble_t ask = {&img_robin_pointing, CHK_TEXT(bedroom__sleepers_ask), CHK_TEXT(next)};
      CHK_TRY(chk_say(&ask, 1), CHK_STEP(BEDROOM_STEP_SETUP));

      const char *rows[] = {
          CHK_TEXT(bedroom__sleepers_one),
          CHK_TEXT(bedroom__sleepers_two),
          CHK_TEXT(bedroom__sleepers_more),
          NULL,
      };

      int offset = 0;
      int chosen = gui_list_strings((int)c->result[CHK_BEDROOM_SLEEPERS] - 1, &offset, rows, CHK_TEXT(next),
                                    CHK_TEXT(back), GUI_INACTION);
      if (chosen < 0) {
        continue;
      }
      c->result[CHK_BEDROOM_SLEEPERS] = (float)(chosen + 1);
      c->step = BEDROOM_STEP_PRECHECK;
    }

    /* Precheck */

    if (c->step == BEDROOM_STEP_PRECHECK) {
      const char *const items[] = {
          CHK_TEXT(bedroom__list_window),
          CHK_TEXT(bedroom__list_door),
          CHK_TEXT(bedroom__list_place),
      };
      CHK_TRY(chk_list(title, CHK_TEXT(stage__baseline), items, 3, back), CHK_STEP(BEDROOM_STEP_SLEEPERS));
      c->step = BEDROOM_STEP_BASELINE;
    }

    /* Baseline */

    // the room before anyone lies down, taken once the reading holds
    if (c->step == BEDROOM_STEP_BASELINE) {
      // the series starts here: the intro and the pickers are not part of the
      // record, so the clock and the record start afresh
      if (c->run.began == 0) {
        chk_restart(c);
      }
      const chk_screen_t baseline = {
          .title = title,
          .stage = CHK_TEXT(stage__baseline),
          .hint = CHK_TEXT(bedroom__baseline_hint),
          .unit = "ppm",
          .show = CHK_SHOW_PROGRESS,
          .back = CHK_TEXT(back),
          .field = AL_SAMPLE_CO2,
          .cfg = {.min_ms = CHK_BEDROOM_SETTLE_MIN_MS, .max_ms = CHK_BEDROOM_SETTLE_MAX_MS, .settle = true},
      };
      CHK_TRY(chk_measure(c, &baseline), CHK_REDO(BEDROOM_STEP_PRECHECK));
      chk_mark(c, CHK_BEDROOM_MARK_SETTLED);

      // the baseline is the newest settled window, or what the store holds
      // when the run ended before one
      c->result[CHK_BEDROOM_C0] = chk_settle_value(&c->run);
      if (isnan(c->result[CHK_BEDROOM_C0])) {
        c->result[CHK_BEDROOM_C0] = chk_vent_baseline_median(CHK_SETTLE_WINDOW);
      }

      // the night is a run of its own
      chk_measure_reset(&c->run);
      c->step = BEDROOM_STEP_TRIGGER;
    }

    /* Trigger */

    if (c->step == BEDROOM_STEP_TRIGGER) {
      // cue the user, unless this is a timer wake re-entering the prompt they
      // left on the nightstand
      if (scr_awake()) {
        al_buzzer_beep(1047, 80, false);
      }
      const chk_bubble_t down = {
          &img_robin_pointing,
          lvx_fmt(CHK_TEXT(bedroom__lie_down), c->result[CHK_BEDROOM_C0]),
          CHK_TEXT(bedroom__lying_down),
      };
      CHK_TRY(chk_say(&down, 1), CHK_REDO(BEDROOM_STEP_BASELINE));

      // the night starts here, and the stretch since the baseline held was
      // the user at the device
      chk_mark(c, CHK_BEDROOM_MARK_ASLEEP);
      chk_bedroom_reset(c);
      c->step = BEDROOM_STEP_NIGHT;
    }

    /* Night */

    // everyone is asleep, so there is nothing to go back to: the key in the
    // morning ends the run, and one pressed too early asks whether to stop
    if (c->step == BEDROOM_STEP_NIGHT) {
      const chk_screen_t night = {
          .title = title,
          .stage = CHK_TEXT(bedroom__stage_night),
          .unit = "ppm",
          .show = CHK_SHOW_CHART,
          .back = CHK_TEXT(bedroom__good_morning),
          .field = AL_SAMPLE_CO2,
          .cfg = {.min_ms = CHK_BEDROOM_MIN_MS, .max_ms = CHK_BEDROOM_MAX_MS},
          .on_sample = bedroom_night_sample,
          .floor = CHK_BEDROOM_OUTDOOR,
          .ends_on_key = true,
          .slot_ms = CHK_BEDROOM_SLOT_MS,
          .redraw_ms = CHK_BEDROOM_REDRAW_MS,
      };
      CHK_TRY(chk_measure(c, &night), CHK_LEAVE);
      chk_mark(c, CHK_BEDROOM_MARK_WOKE);
      c->step = BEDROOM_STEP_RESULT;
      c->phase = RESULT_PHASE_VERDICT;
    }

    /* Result */

    // a night always has a result: what the air did is the answer, and there
    // is no fit to fail
    chk_bedroom_evaluate(c, c->run.elapsed);

    // seal it before saying anything, as the checks above do
    uint16_t stored = chk_record(c, AL_SAMPLE_CO2);

    /* Verdict, stats and share */

    return chk_finish(c, CHK_BEDROOM, stored, on_exit, on_idle, self);
  }
}

/* Bathroom humidity */

// A baseline, a shower and an airing. The outdoor step is the ventilation
// check's, in temperature and humidity rather than CO2, and the recovery is
// its decay fit with the baseline humidity as the floor.

// The record and the payload run at ten seconds: a shower of up to fifteen
// minutes and a recovery of up to thirty outrun both the sample cap and the
// short store's ring at the trial's five.
#define CHK_BATH_RECORD_S 10

// the readings a counted run takes, and the tail of them a median is taken
// over: two minutes outside, one minute of baseline, six readings either way
#define CHK_BATH_OUTSIDE_MS 120000
#define CHK_BATH_OUTSIDE_N 24
#define CHK_BATH_BASELINE_N 12
#define CHK_BATH_MEDIAN_N 6

// The shower: the key ends it, a minute is the floor under which the key is
// taken as a slip, six minutes is where the device asks, and fifteen is where
// it stops waiting.
#define CHK_BATH_SHOWER_MIN_MS 60000
#define CHK_BATH_SHOWER_NUDGE_MS 360000
#define CHK_BATH_SHOWER_MAX_MS 900000

// The recovery: five minutes at least, so the fit has a span, and thirty at
// most, by which point a bathroom that has not dried is the result.
#define CHK_BATH_RECOVERY_MIN_MS 300000
#define CHK_BATH_RECOVERY_MAX_MS 1800000

// fifteen-second slots draw the shower across the chart, thirty the recovery,
// and neither needs the panel redrawn on every reading
#define CHK_BATH_SHOWER_SLOT_MS 15000
#define CHK_BATH_RECOVERY_SLOT_MS 30000
#define CHK_BATH_REDRAW_MS 20000

// Where the flow can be resumed from after a deep sleep.
enum {
  BATH_STEP_INTRO,
  BATH_STEP_OUTDOOR,
  BATH_STEP_OUTSIDE,
  BATH_STEP_PRECHECK,
  BATH_STEP_BASELINE,
  BATH_STEP_TRIGGER,
  BATH_STEP_SHOWER,
  BATH_STEP_OPEN,
  BATH_STEP_RECOVERY,
  BATH_STEP_RESULT,
};

static chk_step_t bath_shower_sample(chk_t *c, float value, int32_t t_ms) {
  (void)t_ms;

  // the shower ends on the key, not on the humidity: how high it goes is the
  // shower's, not the bathroom's
  chk_bath_peak(c, value);

  return CHK_STEP_WAIT;
}

static chk_step_t bath_recovery_sample(chk_t *c, float value, int32_t t_ms) {
  // feed the fit, which keeps watching the peak: humidity often climbs for a
  // minute after the water stops
  chk_bath_observe(c, value, t_ms);

  // enough of the excess gone to call it measured
  float excess0 = c->result[CHK_BATH_PEAK] - c->result[CHK_BATH_RH0];
  float gone = c->result[CHK_BATH_PEAK] - value;
  if (excess0 > 0 && gone >= CHK_BATH_RECOVERY_SHARE * excess0) {
    return CHK_STEP_DONE;
  }

  return gone >= 1.0f ? CHK_STEP_GO : CHK_STEP_WAIT;
}

// the run ends on the share above, so the screen counts down to the same one
static int32_t bath_recovery_remaining(chk_t *c) {
  return chk_bath_remaining(c, CHK_BATH_RECOVERY_SHARE);
}

static void chk_view_bath(const float *r, const int32_t *marks, uint8_t cadence, chk_view_t *v) {
  v->title = CHK_TEXT(bath__title);
  v->check = CHK_CODE_BATHROOM;
  v->signal = AL_SAMPLE_HUM;
  v->note = CHK_TEXT(bath__stat_note);

  // the half-life follows from the rate, and carries the rate's own band
  int half = chk_round_minutes(chk_half_life(r[CHK_BATH_K]));
  int band = chk_round_minutes(chk_half_life_band(r[CHK_BATH_K], r[CHK_BATH_K_BAND]));
  v->lines[0] = lvx_fmt(CHK_TEXT(bath__stat_baseline), r[CHK_BATH_RH0]);
  v->lines[1] = lvx_fmt(CHK_TEXT(bath__stat_peak), r[CHK_BATH_PEAK]);
  v->lines[2] = lvx_fmt(CHK_TEXT(bath__stat_end), r[CHK_BATH_RH1]);
  v->lines[3] = lvx_fmt(CHK_TEXT(bath__stat_half), half, band);
  v->lines[4] = lvx_fmt(CHK_TEXT(bath__stat_temp), r[CHK_BATH_T0], r[CHK_BATH_T1]);
  v->lines[5] = r[CHK_BATH_OUT_HOW] == CHK_BATH_OUT_MEASURED
                    ? lvx_fmt(CHK_TEXT(bath__stat_out), r[CHK_BATH_TOUT], r[CHK_BATH_RHOUT])
                    : CHK_TEXT(bath__stat_out_none);
  v->num_lines = 6;

  // k, kSe, r2, rh0, rhPeak, rh1, t0, t1, outHow, tout20, rhOut, outAge
  v->payload[0] = r[CHK_BATH_K];
  v->payload[1] = r[CHK_BATH_K_BAND];
  v->payload[2] = r[CHK_BATH_R2];
  v->payload[3] = r[CHK_BATH_RH0];
  v->payload[4] = r[CHK_BATH_PEAK];
  v->payload[5] = r[CHK_BATH_RH1];
  v->payload[6] = r[CHK_BATH_T0];
  v->payload[7] = r[CHK_BATH_T1];
  v->payload[8] = r[CHK_BATH_OUT_HOW];
  // the outdoor temperature rides twenty degrees up, so the field carries a
  // frosty morning without a sign bit; nothing measured rides as zero
  v->payload[9] = r[CHK_BATH_OUT_HOW] == CHK_BATH_OUT_MEASURED ? r[CHK_BATH_TOUT] + 20.0f : 0;
  v->payload[10] = r[CHK_BATH_RHOUT];
  v->payload[11] = r[CHK_BATH_OUT_AGE];
  v->num_payload = 12;
  // the baseline, the shower on and done, the window opened and the recovery
  // stopped, which is where the page bands the chart
  chk_view_marks(marks, CHK_BATH_MARKS, cadence, v);

  // the verdict: how long the moisture took to halve, and the advice the tier
  // earns
  chk_bath_tier_t tier = chk_bath_tier(chk_half_life(r[CHK_BATH_K]));
  const char *advice = tier == CHK_BATH_TIER_LOW   ? CHK_TEXT(bath__advice_low)
                       : tier == CHK_BATH_TIER_MID ? CHK_TEXT(bath__advice_mid)
                                                   : CHK_TEXT(bath__advice_high);
  v->verdict[0] = (chk_bubble_t){tier == CHK_BATH_TIER_HIGH ? &img_robin_happy : &img_robin_standing,
                                 lvx_fmt(CHK_TEXT(bath__verdict), half), CHK_TEXT(next)};
  v->verdict[1] = (chk_bubble_t){&img_robin_pointing, advice, CHK_TEXT(next)};
  v->num_verdict = 2;
}

void *chk_bath_run(void *on_exit, void *on_idle, void *self) {
  // pick the check up where it stopped, or start it
  chk_t *c = chk_context();
  if (!chk_resuming(c, CHK_BATH)) {
    chk_begin(c, CHK_BATH);
    // a bathroom check runs in the bathroom by definition, so it is not asked
    c->room = CHK_CODE_ROOM_BATHROOM;
  }
  // the record runs slower than the sensor, which is what keeps a shower and
  // its recovery inside the sample cap
  c->record = CHK_BATH_RECORD_S;
  chk_park_into(self);

  const char *title = CHK_TEXT(bath__title);

  // the step loop, as the checks above have it
  bool back = false;
  for (;;) {
    /* Introduction */

    if (c->step == BATH_STEP_INTRO) {
      const chk_bubble_t intro[] = {
          {&img_robin_happy, CHK_TEXT(bath__intro_1), CHK_TEXT(next)},
          {&img_robin_pointing, CHK_TEXT(bath__intro_2), CHK_TEXT(next)},
          {&img_robin_standing, CHK_TEXT(bath__intro_3), CHK_TEXT(start)},
      };
      size_t n = sizeof(intro) / sizeof(intro[0]);
      CHK_TRY(chk_say_from(intro, n, back ? n - 1 : 0), CHK_LEAVE);
      c->step = BATH_STEP_OUTDOOR;
    }

    /* Outdoor air */

    // what the airing had to work with: a remembered reading while it is
    // fresh, a walk outside, or nothing at all. Skipping is allowed, and the
    // page then compares against the room's own baseline alone.
    if (c->step == BATH_STEP_OUTDOOR) {
      const chk_bubble_t ask = {&img_robin_pointing, CHK_TEXT(bath__outdoor_ask), CHK_TEXT(next)};
      CHK_TRY(chk_say(&ask, 1), CHK_STEP(BATH_STEP_INTRO));

      // the remembered row is only there when there is something to remember,
      // so the rows are numbered as they are built
      float tmp = 0, rh = 0, age = 0;
      bool held = chk_bath_outdoor_get(al_clock_get_epoch(), &tmp, &rh, &age);
      const char *rows[4] = {0};
      int num = 0;
      int use = -1;
      if (held) {
        rows[num] = lvx_fmt(CHK_TEXT(bath__outdoor_remembered), tmp, rh, chk_vent_age_text(age));
        use = num++;
      }
      int measure = num;
      rows[num++] = CHK_TEXT(bath__outdoor_measure);
      rows[num++] = CHK_TEXT(bath__outdoor_skip);

      // the list cannot tell leaving from timing out, so both go back to the
      // bubble, which times out on its own if nobody is there
      int offset = 0;
      int chosen = gui_list_strings(0, &offset, rows, CHK_TEXT(next), CHK_TEXT(back), GUI_INACTION);
      if (chosen < 0) {
        continue;
      }
      if (chosen == measure) {
        c->step = BATH_STEP_OUTSIDE;
      } else {
        bool kept = chosen == use;
        c->result[CHK_BATH_OUT_HOW] = (float)(kept ? CHK_BATH_OUT_MEASURED : CHK_BATH_OUT_SKIPPED);
        c->result[CHK_BATH_TOUT] = kept ? tmp : 0;
        c->result[CHK_BATH_RHOUT] = kept ? rh : 0;
        c->result[CHK_BATH_OUT_AGE] = kept ? age : 0;
        c->step = BATH_STEP_PRECHECK;
      }
    }

    /* Outside */

    if (c->step == BATH_STEP_OUTSIDE) {
      const chk_bubble_t go = {&img_robin_pointing, CHK_TEXT(bath__go_outside), CHK_TEXT(start)};
      CHK_TRY(chk_say(&go, 1), CHK_STEP(BATH_STEP_OUTDOOR));

      // a counted run rather than a settling one: the settle classifier's
      // band is ten ppm of CO2 and means nothing in degrees
      const chk_screen_t outside = {
          .title = title,
          .stage = CHK_TEXT(stage__outside),
          .hint = CHK_TEXT(bath__outside_hint),
          .unit = "C",
          .show = CHK_SHOW_PROGRESS,
          .back = CHK_TEXT(back),
          .field = AL_SAMPLE_TMP,
          .cfg = {.max_ms = CHK_BATH_OUTSIDE_MS, .capacity = CHK_BATH_OUTSIDE_N},
          .on_sample = chk_baseline_sample,
      };
      CHK_TRY(chk_measure(c, &outside), (chk_measure_reset(&c->run), CHK_STEP(BATH_STEP_OUTSIDE)));

      // the tail of the run, by which point the sensor has caught up with the
      // air outside
      float tmp = chk_bath_median(AL_SAMPLE_TMP, CHK_BATH_MEDIAN_N);
      float rh = chk_bath_median(AL_SAMPLE_HUM, CHK_BATH_MEDIAN_N);
      chk_bath_outdoor_set(tmp, rh, al_clock_get_epoch());
      c->result[CHK_BATH_OUT_HOW] = (float)CHK_BATH_OUT_MEASURED;
      c->result[CHK_BATH_TOUT] = tmp;
      c->result[CHK_BATH_RHOUT] = rh;
      c->result[CHK_BATH_OUT_AGE] = 0;

      // the walk outside is not part of the check's own series, so the run
      // ends here and the baseline starts the clock again
      chk_measure_reset(&c->run);
      c->step = BATH_STEP_PRECHECK;

      const chk_bubble_t got = {&img_robin_happy, lvx_fmt(CHK_TEXT(bath__outdoor_got), tmp, rh), CHK_TEXT(next)};
      CHK_TRY(chk_say(&got, 1), CHK_STEP(BATH_STEP_OUTSIDE));
    }

    /* Precheck */

    if (c->step == BATH_STEP_PRECHECK) {
      const char *const items[] = {
          CHK_TEXT(bath__list_window),
          CHK_TEXT(bath__list_door),
          CHK_TEXT(bath__list_place),
      };
      CHK_TRY(chk_list(title, CHK_TEXT(stage__baseline), items, 3, back), CHK_STEP(BATH_STEP_OUTDOOR));
      c->step = BATH_STEP_BASELINE;
    }

    /* Baseline */

    // the room before the shower, as a count rather than a settling run: the
    // humidity of a dry bathroom is where it is, and a minute of it is enough
    if (c->step == BATH_STEP_BASELINE) {
      // the series starts here, as in the checks above
      if (c->run.began == 0) {
        chk_restart(c);
      }
      const chk_screen_t baseline = {
          .title = title,
          .stage = CHK_TEXT(stage__baseline),
          .hint = CHK_TEXT(bath__baseline_hint),
          .unit = "%",
          .show = CHK_SHOW_PROGRESS,
          .back = CHK_TEXT(back),
          .field = AL_SAMPLE_HUM,
          .cfg = {.capacity = CHK_BATH_BASELINE_N},
          .on_sample = chk_baseline_sample,
      };
      CHK_TRY(chk_measure(c, &baseline), CHK_REDO(BATH_STEP_PRECHECK));
      chk_mark(c, CHK_BATH_MARK_SETTLED);

      c->result[CHK_BATH_RH0] = chk_bath_median(AL_SAMPLE_HUM, CHK_BATH_MEDIAN_N);
      c->result[CHK_BATH_T0] = chk_bath_median(AL_SAMPLE_TMP, CHK_BATH_MEDIAN_N);
      c->result[CHK_BATH_PEAK] = c->result[CHK_BATH_RH0];

      // the shower is a run of its own
      chk_measure_reset(&c->run);
      c->step = BATH_STEP_TRIGGER;
    }

    /* Trigger */

    if (c->step == BATH_STEP_TRIGGER) {
      // cue the user, unless this is a timer wake re-entering the prompt
      if (scr_awake()) {
        al_buzzer_beep(1047, 80, false);
      }
      const chk_bubble_t shower = {
          &img_robin_pointing,
          lvx_fmt(CHK_TEXT(bath__shower_ask), c->result[CHK_BATH_RH0]),
          CHK_TEXT(bath__shower_is_on),
      };
      CHK_TRY(chk_say(&shower, 1), CHK_REDO(BATH_STEP_BASELINE));

      // the water is running now
      chk_mark(c, CHK_BATH_MARK_SHOWER_ON);
      c->step = BATH_STEP_SHOWER;
    }

    /* Shower */

    // the water is on, so there is no going back from here, only stopping
    if (c->step == BATH_STEP_SHOWER) {
      const chk_screen_t shower = {
          .title = title,
          .stage = CHK_TEXT(bath__stage_shower),
          .nudge = CHK_TEXT(bath__shower_nudge),
          .unit = "%",
          .show = CHK_SHOW_CHART,
          .back = CHK_TEXT(bath__shower_done),
          .field = AL_SAMPLE_HUM,
          .cfg = {.min_ms = CHK_BATH_SHOWER_MIN_MS,
                  .max_ms = CHK_BATH_SHOWER_MAX_MS,
                  .nudge_ms = CHK_BATH_SHOWER_NUDGE_MS},
          .on_sample = bath_shower_sample,
          .floor = c->result[CHK_BATH_RH0],
          .ends_on_key = true,
          .slot_ms = CHK_BATH_SHOWER_SLOT_MS,
          .redraw_ms = CHK_BATH_REDRAW_MS,
      };
      CHK_TRY(chk_measure(c, &shower), CHK_LEAVE);
      chk_mark(c, CHK_BATH_MARK_SHOWER_DONE);

      // the recovery is a run of its own, timed from the window opening
      chk_measure_reset(&c->run);
      c->step = BATH_STEP_OPEN;
    }

    /* Open the window */

    if (c->step == BATH_STEP_OPEN) {
      if (scr_awake()) {
        al_buzzer_beep(1047, 80, false);
      }
      const chk_bubble_t open = {&img_robin_pointing, CHK_TEXT(bath__open_window), CHK_TEXT(bath__window_is_open)};
      CHK_TRY(chk_say(&open, 1), CHK_LEAVE);

      // the window goes open now: the recovery starts here, and the stretch
      // since the shower ended was the user at the device
      chk_mark(c, CHK_BATH_MARK_OPENED);
      c->step = BATH_STEP_RECOVERY;
    }

    /* Recovery */

    if (c->step == BATH_STEP_RECOVERY) {
      const chk_screen_t recovery = {
          .title = title,
          .stage = CHK_TEXT(bath__stage_airing),
          .unit = "%",
          .show = CHK_SHOW_CHART,
          .field = AL_SAMPLE_HUM,
          .cfg = {.min_ms = CHK_BATH_RECOVERY_MIN_MS, .max_ms = CHK_BATH_RECOVERY_MAX_MS},
          .on_sample = bath_recovery_sample,
          .on_remaining = bath_recovery_remaining,
          .floor = c->result[CHK_BATH_RH0],
          .slot_ms = CHK_BATH_RECOVERY_SLOT_MS,
          .redraw_ms = CHK_BATH_REDRAW_MS,
      };
      CHK_TRY(chk_measure(c, &recovery), CHK_LEAVE);
      chk_mark(c, CHK_BATH_MARK_STOPPED);
      c->step = BATH_STEP_RESULT;
      c->phase = RESULT_PHASE_VERDICT;
    }

    /* Result */

    // the room at the end, beside the humidity the fit was made of
    c->result[CHK_BATH_T1] = chk_bath_median(AL_SAMPLE_TMP, CHK_BATH_MEDIAN_N);

    chk_bath_quality_t quality = chk_bath_evaluate(c, c->run.elapsed);

    // no number worth reporting: say which way the moisture went and offer
    // another go
    if (quality != CHK_BATH_SOLID) {
      const char *direction = quality == CHK_BATH_QUICK ? CHK_TEXT(vent__quickly) : CHK_TEXT(vent__slowly);
      const chk_bubble_t unclear = {
          &img_robin_standing,
          lvx_fmt(CHK_TEXT(bath__unclear), direction),
          CHK_TEXT(again),
      };
      return chk_leave(c, chk_say(&unclear, 1), on_exit, on_idle, self);
    }

    // seal it before saying anything, as the checks above do
    uint16_t stored = chk_record(c, AL_SAMPLE_HUM);

    /* Verdict, stats and share */

    return chk_finish(c, CHK_BATH, stored, on_exit, on_idle, self);
  }
}

/* View */

const char *chk_name(uint8_t id) {
  switch (id) {
    case CHK_VENT:
      return CHK_TEXT(vent__name);
    case CHK_STOVE:
      return CHK_TEXT(stove__name);
    case CHK_BEDROOM:
      return CHK_TEXT(bedroom__name);
    case CHK_BATH:
      return CHK_TEXT(bath__name);
    default:
      return NULL;
  }
}

bool chk_describe(uint8_t id, const float *result, const int32_t *marks, uint8_t marks_len, uint8_t cadence,
                  chk_view_t *out) {
  if (result == NULL || out == NULL || marks == NULL || marks_len < 4) {
    return false;
  }

  memset(out, 0, sizeof(*out));

  switch (id) {
    case CHK_VENT:
      chk_view_vent(result, marks, cadence, out);
      return true;
    case CHK_STOVE:
      chk_view_stove(result, marks, cadence, out);
      return true;
    case CHK_BEDROOM:
      chk_view_bedroom(result, marks, cadence, out);
      return true;
    case CHK_BATH:
      chk_view_bath(result, marks, cadence, out);
      return true;
    default:
      return false;
  }
}
