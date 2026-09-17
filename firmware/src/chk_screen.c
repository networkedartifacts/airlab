// The checks as the user walks through them, and what a finished one looks
// like. The screens and the policy are the kit's (chk_ui.c); the maths is
// chk_vent.c's and chk_stove.c's; what is here is each flow, its copy, the
// numbers it chooses, and the view it and a reopened record share.

#include <stdbool.h>
#include <string.h>

#include <al/buzzer.h>

#include "chk.h"
#include "chk_stove.h"
#include "chk_vent.h"
#include "gui.h"
#include "img.h"
#include "lvx.h"
#include "scr.h"

/* Shared */

// Every way out of a kit call but "next" releases the check. Escape is the
// user abandoning it; a timeout only reaches here before the baseline, when
// the user was looking rather than waiting, so the next run starts over too.
// A started check never times out: its prompts park the device on the flow's
// screen and wake back into it. Expects `c`, `on_exit`, `on_idle` and `self`
// in scope.
#define CHK_TRY(expr)         \
  do {                        \
    chk_result_t _r = (expr); \
    if (_r == CHK_NEXT) {     \
      break;                  \
    }                         \
    chk_release(c);           \
    if (_r == CHK_IDLE) {     \
      return on_idle;         \
    }                         \
    if (_r == CHK_AGAIN) {    \
      return self;            \
    }                         \
    return on_exit;           \
  } while (0)

// Releases the check after a closing bubble and picks where to land: a
// timeout goes idle, "again" restarts when the caller offers a screen for it,
// and anything else leaves.
static void *chk_leave(chk_t *c, chk_result_t said, void *on_exit, void *on_idle, void *again) {
  chk_release(c);
  if (said == CHK_IDLE) return on_idle;
  if (said == CHK_NEXT && again != NULL) return again;
  return on_exit;
}

// A baseline accumulates nothing: the run's own count ends it, and the value
// is read back from the store afterwards as a median.
static chk_step_t chk_baseline_sample(chk_t *c, float value, int32_t t_ms) {
  (void)c;
  (void)value;
  (void)t_ms;
  return CHK_STEP_WAIT;
}

// The result step is shown in phases, so a wake out of a parked sleep lands
// on the screen that was showing rather than back at the verdict.
enum {
  RESULT_PHASE_VERDICT,
  RESULT_PHASE_STATS,
  RESULT_PHASE_SHARE,
};

// The end every solid result shares: the stats, then the code. The record was
// just written, so reading it back means the result shown now is built from
// exactly what a reopened check will be built from later; the live block is
// the fallback for a record that could not be kept.
static void *chk_finish(chk_t *c, uint8_t id, uint16_t stored, void *on_exit, void *on_idle, void *self) {
  chk_view_t view;
  if (!chk_view_of(stored, &view) &&
      !chk_describe(id, c->result, c->marks, CHK_MARKS, (uint8_t)chk_cadence(), &view)) {
    chk_release(c);
    return on_exit;
  }
  if (c->phase <= RESULT_PHASE_STATS) {
    c->phase = RESULT_PHASE_STATS;
    CHK_TRY(chk_stats(view.title, CHK_TEXT(stage__results), view.lines, view.num_lines, view.note));
  }
  c->phase = RESULT_PHASE_SHARE;
  CHK_TRY(chk_show_code(stored));
  chk_release(c);
  return on_exit;
}

// how many samples fall between two phase boundaries, at this cadence
static float chk_view_span(int32_t from_ms, int32_t to_ms, uint8_t cadence) {
  if (cadence == 0 || to_ms <= from_ms) {
    return 0;
  }
  return (float)((to_ms - from_ms) / 1000 / cadence);
}

/* Ventilation */

// The baseline is six readings, which at the device's cadence is about half a
// minute of closed-window air.
#define CHK_VENT_BASELINE_N 6

// Where the flow can be resumed from after a deep sleep, which on this device
// is a reset: main memory is gone and the flow is re-entered from the top, so
// each step records that it is done before moving on.
enum {
  VENT_STEP_INTRO,
  VENT_STEP_PRECHECK,
  VENT_STEP_OUTDOOR,
  VENT_STEP_BASELINE,
  VENT_STEP_TRIGGER,
  VENT_STEP_MEASURE,
  VENT_STEP_RESULT,
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
// 600 ppm and a 50 ppm error moves the estimate by about a third. The wheel
// opens on what the device has seen, and the precheck wants this much excess
// above it before there is anything to measure.
#define CHK_VENT_EXCESS_MIN 200.0f

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

static void chk_view_vent(const float *r, const int32_t *marks, uint8_t cadence, chk_view_t *v) {
  v->title = CHK_TEXT(vent__title);
  v->letter = 'A';
  v->signal = AL_SAMPLE_CO2;
  v->note = CHK_TEXT(vent__stat_note);

  int half = chk_round_minutes(r[CHK_VENT_HALF_LIFE]);
  v->lines[0] = lvx_fmt(CHK_TEXT(vent__stat_ach), r[CHK_VENT_ACH], r[CHK_VENT_ACH_BAND]);
  v->lines[1] = lvx_fmt(CHK_TEXT(vent__stat_half_life), half);
  v->lines[2] = lvx_fmt(CHK_TEXT(vent__stat_fresh), chk_round_minutes(r[CHK_VENT_FRESH]));
  v->lines[3] = lvx_fmt(CHK_TEXT(vent__stat_co2), r[CHK_VENT_C0], r[CHK_VENT_CLAST], r[CHK_VENT_COUT]);
  v->num_lines = 4;

  // ach, achSe, r2, c0, c1, cout, pre
  v->payload[0] = r[CHK_VENT_ACH];
  v->payload[1] = r[CHK_VENT_ACH_BAND];
  v->payload[2] = r[CHK_VENT_R2];
  v->payload[3] = r[CHK_VENT_C0];
  v->payload[4] = r[CHK_VENT_CLAST];
  v->payload[5] = r[CHK_VENT_COUT];
  // the baseline samples at the head of the series, which is where the page
  // stops shading the chart as "before the window opened"
  v->payload[6] = chk_view_span(0, marks[0], cadence);
  v->num_payload = 7;

  // the one number the verdict is about
  v->headline = lvx_fmt(CHK_TEXT(vent__verdict_half_life), half);
}

void *chk_vent_run(void *on_exit, void *on_idle, void *self) {
  // pick the check up where it stopped, or start it
  chk_t *c = chk_context();
  if (!chk_resuming(c, CHK_VENT)) {
    chk_begin(c, CHK_VENT);
  }
  chk_park_into(self);

  const char *title = CHK_TEXT(vent__title);

  /* Introduction */

  if (c->step == VENT_STEP_INTRO) {
    const chk_bubble_t intro[] = {
        {&img_robin_happy, CHK_TEXT(vent__intro_1), CHK_TEXT(next)},
        {&img_robin_pointing, CHK_TEXT(vent__intro_2), CHK_TEXT(next)},
        {&img_robin_pointing, CHK_TEXT(vent__intro_3), CHK_TEXT(next)},
        {&img_robin_standing, CHK_TEXT(vent__intro_4), CHK_TEXT(start)},
    };
    CHK_TRY(chk_say(intro, sizeof(intro) / sizeof(intro[0])));
    c->step = VENT_STEP_PRECHECK;
  }

  /* Precheck */

  // the single-zone assumption the decay rests on is the user's to meet
  if (c->step == VENT_STEP_PRECHECK) {
    const char *const items[] = {
        CHK_TEXT(vent__list_windows),
        CHK_TEXT(vent__list_door),
        CHK_TEXT(vent__list_table),
    };
    CHK_TRY(chk_list(title, CHK_TEXT(stage__baseline), items, 3));
    c->step = VENT_STEP_OUTDOOR;
  }

  if (c->step == VENT_STEP_OUTDOOR) {
    // outdoor CO2, opened on the lowest the device has lately seen. The wheel
    // cannot tell leaving from timing out, so both release the check.
    int outdoor = (int)chk_vent_outdoor_guess();
    if (!gui_wheel(CHK_TEXT(vent__outdoor), &outdoor, 380, 10, 700, CHK_TEXT(next), CHK_TEXT(back), "%d ppm",
                   GUI_INACTION)) {
      chk_release(c);
      return on_exit;
    }
    c->result[CHK_VENT_COUT] = (float)outdoor;
    c->step = VENT_STEP_BASELINE;
  }

  /* Baseline */

  if (c->step == VENT_STEP_BASELINE) {
    const chk_screen_t baseline = {
        .title = title,
        .stage = CHK_TEXT(stage__baseline),
        .hint = CHK_TEXT(vent__baseline_hint),
        .unit = "ppm",
        .show = CHK_SHOW_PROGRESS,
        .field = AL_SAMPLE_CO2,
        .cfg = {.capacity = CHK_VENT_BASELINE_N},
        .on_sample = chk_baseline_sample,
    };
    CHK_TRY(chk_measure(c, &baseline));

    // the baseline is the median of what the store holds, which is steadier
    // than a mean when a reading or two is off
    c->result[CHK_VENT_C0] = chk_vent_baseline_median(CHK_VENT_BASELINE_N);

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
    CHK_TRY(chk_say(&open, 1));

    // the boundary between the closed-window baseline and the decay, which is
    // where the page bands the chart
    chk_mark(c);
    c->step = VENT_STEP_MEASURE;
  }

  /* Measurement */

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
        .floor = c->result[CHK_VENT_COUT],
    };
    CHK_TRY(chk_measure(c, &decay));
    c->step = VENT_STEP_RESULT;
    c->phase = RESULT_PHASE_VERDICT;
  }

  /* Result */

  chk_vent_quality_t quality = chk_vent_evaluate(c, c->run.elapsed);

  // no number worth reporting: say which way the air went and offer another go
  if (quality != CHK_VENT_SOLID) {
    const char *direction = quality == CHK_VENT_QUICK ? CHK_TEXT(vent__quickly) : CHK_TEXT(vent__slowly);
    const chk_bubble_t unclear = {
        &img_robin_standing,
        lvx_fmt(CHK_TEXT(vent__unclear), direction),
        CHK_TEXT(again),
    };
    return chk_leave(c, chk_say(&unclear, 1), on_exit, on_idle, self);
  }

  // seal it before saying anything: walking away from the verdict should not
  // lose the check. A result step re-entered afterwards gets the same record.
  uint16_t stored = chk_record(c, AL_SAMPLE_CO2);

  // the verdict: the one number, and the advice the tier earns
  int half = chk_round_minutes(c->result[CHK_VENT_HALF_LIFE]);
  chk_vent_tier_t tier = chk_vent_tier(c->result[CHK_VENT_ACH]);
  const char *advice = tier == CHK_VENT_TIER_LOW   ? CHK_TEXT(vent__advice_low)
                       : tier == CHK_VENT_TIER_MID ? CHK_TEXT(vent__advice_mid)
                                                   : CHK_TEXT(vent__advice_high);
  const chk_bubble_t verdict[] = {
      {tier == CHK_VENT_TIER_HIGH ? &img_robin_happy : &img_robin_standing,
       lvx_fmt(CHK_TEXT(vent__verdict_half_life), half), CHK_TEXT(next)},
      {&img_robin_pointing, advice, CHK_TEXT(next)},
  };
  if (c->phase == RESULT_PHASE_VERDICT) {
    CHK_TRY(chk_say(verdict, 2));
  }

  /* Stats and share */

  return chk_finish(c, CHK_VENT, stored, on_exit, on_idle, self);
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
  v->letter = 'E';
  v->signal = AL_SAMPLE_CO2;
  v->note = CHK_TEXT(stove__stat_note);

  int percent = (int)(r[CHK_STOVE_CAPTURE] * 100 + 0.5f);
  v->lines[0] = lvx_fmt(CHK_TEXT(stove__stat_capture), percent);
  v->lines[1] = r[CHK_STOVE_HOOD_ACH] > 0 ? lvx_fmt(CHK_TEXT(stove__stat_hood), r[CHK_STOVE_HOOD_ACH])
                                          : CHK_TEXT(stove__stat_hood_none);
  v->lines[2] = lvx_fmt(CHK_TEXT(stove__stat_peak), r[CHK_STOVE_PEAK]);
  v->num_lines = 3;

  // ce, hoodAch, slope1, slope3, c0, noxPeak, pre, pass1, pass2, pass3
  v->payload[0] = (float)percent;
  v->payload[1] = r[CHK_STOVE_HOOD_ACH] > 0 ? r[CHK_STOVE_HOOD_ACH] : 0;
  v->payload[2] = r[CHK_STOVE_SLOPE1];
  v->payload[3] = r[CHK_STOVE_SLOPE3];
  v->payload[4] = r[CHK_STOVE_C0];
  v->payload[5] = r[CHK_STOVE_NOX];
  // the baseline, then a count per pass, from the boundaries the flow marked
  v->payload[6] = chk_view_span(0, marks[0], cadence);
  v->payload[7] = chk_view_span(marks[0], marks[1], cadence);
  v->payload[8] = chk_view_span(marks[1], marks[2], cadence);
  v->payload[9] = chk_view_span(marks[2], marks[3], cadence);
  v->num_payload = 10;

  v->headline = lvx_fmt(CHK_TEXT(stove__verdict), percent);
}

void *chk_stove_run(void *on_exit, void *on_idle, void *self) {
  // pick the check up where it stopped, or start it
  chk_t *c = chk_context();
  if (!chk_resuming(c, CHK_STOVE)) {
    chk_begin(c, CHK_STOVE);
  }
  chk_park_into(self);

  const char *title = CHK_TEXT(stove__title);

  /* Introduction */

  if (c->step == STOVE_STEP_INTRO) {
    const chk_bubble_t intro[] = {
        {&img_robin_happy, CHK_TEXT(stove__intro_1), CHK_TEXT(next)},
        {&img_robin_pointing, CHK_TEXT(stove__intro_2), CHK_TEXT(next)},
        {&img_robin_standing, CHK_TEXT(stove__intro_3), CHK_TEXT(start)},
    };
    CHK_TRY(chk_say(intro, sizeof(intro) / sizeof(intro[0])));
    c->step = STOVE_STEP_PRECHECK;
  }

  /* Precheck */

  if (c->step == STOVE_STEP_PRECHECK) {
    const char *const items[] = {
        CHK_TEXT(stove__list_off),
        CHK_TEXT(stove__list_pot),
        CHK_TEXT(stove__list_away),
    };
    CHK_TRY(chk_list(title, CHK_TEXT(stage__baseline), items, 3));
    c->step = STOVE_STEP_BASELINE;
  }

  /* Baseline */

  if (c->step == STOVE_STEP_BASELINE) {
    const chk_screen_t baseline = {
        .title = title,
        .stage = CHK_TEXT(stage__baseline),
        .hint = CHK_TEXT(stove__baseline_hint),
        .unit = "ppm",
        .show = CHK_SHOW_PROGRESS,
        .field = AL_SAMPLE_CO2,
        .cfg = {.capacity = CHK_STOVE_BASELINE_N},
        .on_sample = chk_baseline_sample,
    };
    CHK_TRY(chk_measure(c, &baseline));

    c->result[CHK_STOVE_C0] = chk_vent_baseline_median(CHK_STOVE_BASELINE_N);
    c->result[CHK_STOVE_PEAK] = c->result[CHK_STOVE_C0];

    // the boundary between the baseline and the first pass, which is a run
    // of its own
    chk_mark(c);
    chk_measure_reset(&c->run);
    c->phase = 0;
    c->step = STOVE_STEP_PASSES;
  }

  /* Three passes */

  const chk_stove_pass_t passes[] = {
      {CHK_TEXT(stove__pass_open), CHK_TEXT(stove__burner_on), CHK_TEXT(stove__stage_open), true},
      {CHK_TEXT(stove__pass_clear), CHK_TEXT(stove__hood_on), CHK_TEXT(stove__stage_clear), false},
      {CHK_TEXT(stove__pass_hood), CHK_TEXT(stove__burner_on), CHK_TEXT(stove__stage_hood), true},
  };

  for (int i = c->step == STOVE_STEP_PASSES ? c->phase : 3; i < 3; i++) {
    const chk_stove_pass_t *pass = &passes[i];

    // the callback reads the pass back off the context, and a resume comes
    // back into the pass it left
    c->phase = (uint8_t)i;

    // the prompt, unless the pass is already under way: a resume into a
    // measurement must not ask for the burner again
    if (c->run.began == 0) {
      if (scr_awake()) {
        al_buzzer_beep(1047, 80, false);
      }
      const chk_bubble_t prompt = {&img_robin_pointing, pass->prompt, pass->action};
      CHK_TRY(chk_say(&prompt, 1));
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
        .cfg = {.min_ms = CHK_STOVE_SLOPE_MS,
                .max_ms = CHK_STOVE_PASS_MAX_MS,
                .nudge_ms = CHK_STOVE_SLOPE_MS},
        .on_sample = stove_sample,
        .floor = c->result[CHK_STOVE_C0],
        .range = pass->burning ? CHK_STOVE_BURN_RISE : c->result[CHK_STOVE_PEAK] - c->result[CHK_STOVE_C0],
    };
    CHK_TRY(chk_measure(c, &measure));

    // where this pass ended, so the page can band the chart per pass
    chk_mark(c);

    // the kitchen has had enough: stop the check rather than the pass
    if (c->result[CHK_STOVE_PEAK] >= CHK_STOVE_ABORT_PPM) {
      const chk_bubble_t stop = {&img_robin_angry1, CHK_TEXT(stove__too_much), CHK_TEXT(ok)};
      return chk_leave(c, chk_say(&stop, 1), on_exit, on_idle, NULL);
    }

    // the next pass starts fresh
    chk_measure_reset(&c->run);
  }
  if (c->step != STOVE_STEP_RESULT) {
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

  int percent = (int)(c->result[CHK_STOVE_CAPTURE] * 100 + 0.5f);
  chk_stove_tier_t tier = chk_stove_tier(c->result[CHK_STOVE_CAPTURE]);
  const char *advice = tier == CHK_STOVE_TIER_LOW   ? CHK_TEXT(stove__advice_low)
                       : tier == CHK_STOVE_TIER_MID ? CHK_TEXT(stove__advice_mid)
                                                    : CHK_TEXT(stove__advice_high);

  const chk_bubble_t verdict[] = {
      {tier == CHK_STOVE_TIER_HIGH ? &img_robin_happy : &img_robin_standing,
       lvx_fmt(CHK_TEXT(stove__verdict), percent), CHK_TEXT(next)},
      {&img_robin_pointing, advice, CHK_TEXT(next)},
  };
  if (c->phase == RESULT_PHASE_VERDICT) {
    CHK_TRY(chk_say(verdict, 2));
  }

  /* Stats and share */

  return chk_finish(c, CHK_STOVE, stored, on_exit, on_idle, self);
}

/* View */

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
    default:
      return false;
  }
}
