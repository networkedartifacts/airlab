// The ventilation check as the user walks through it. The screens and the
// policy are the kit's; the maths is chk_vent.c's; what is here is the flow,
// the copy and the numbers this check chooses.

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include <al/buzzer.h>
#include <al/store.h>
#include <al/sensor.h>

#include "chk.h"
#include "chk_vent.h"
#include "gui.h"
#include "img.h"
#include "lvx.h"

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

static chk_step_t vent_baseline_sample(chk_t *c, float value, int32_t t_ms) {
  // the baseline is the median, so accumulate nothing: the run's own count
  // ends it, and the value is read back from the store afterwards
  (void)c;
  (void)value;
  (void)t_ms;
  return CHK_STEP_WAIT;
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

void *chk_vent_run(void *on_exit, void *on_idle, void *self) {
  // pick the check up where it stopped, or start it
  chk_t *c = chk_context();
  if (!chk_resuming(c, CHK_VENT)) {
    chk_begin(c, CHK_VENT);
  }

  const char *title = CHK_TEXT(vent__title);

// Leaving and timing out are not the same thing. A user who presses escape
// has abandoned the check, so the context is released and the next run starts
// over. A timeout is the device going to sleep with the user still intending
// to finish, so the context is kept and the flow resumes into the step it
// was on.
#define VENT_TRY(expr)                    \
  do {                                    \
    chk_result_t _r = (expr);             \
    if (_r == CHK_EXIT) {                 \
      chk_release(c);                         \
      return on_exit;                     \
    }                                     \
    if (_r == CHK_IDLE) return on_idle;   \
    if (_r == CHK_AGAIN) {                \
      chk_release(c);                         \
      return self;                        \
    }                                     \
  } while (0)

  /* Introduction */

  if (c->step == VENT_STEP_INTRO) {
    const chk_bubble_t intro[] = {
        {&img_robin_happy, CHK_TEXT(vent__intro_1), CHK_TEXT(next)},
        {&img_robin_pointing, CHK_TEXT(vent__intro_2), CHK_TEXT(next)},
        {&img_robin_pointing, CHK_TEXT(vent__intro_3), CHK_TEXT(next)},
        {&img_robin_standing, CHK_TEXT(vent__intro_4), CHK_TEXT(start)},
    };
    VENT_TRY(chk_say(intro, sizeof(intro) / sizeof(intro[0])));
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
    VENT_TRY(chk_list(title, CHK_TEXT(stage__baseline), items, 3));
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
      .on_sample = vent_baseline_sample,
  };
  VENT_TRY(chk_measure(c, &baseline, self));

    // the baseline is the median of what the store holds, which is steadier
    // than a mean when a reading or two is off
    c->result[CHK_VENT_C0] = chk_vent_baseline_median(CHK_VENT_BASELINE_N);

    // there has to be something above outdoor to watch leave
    if (c->result[CHK_VENT_C0] - c->result[CHK_VENT_COUT] < CHK_VENT_EXCESS_MIN) {
      const chk_bubble_t fresh = {&img_robin_standing, CHK_TEXT(vent__already_fresh), CHK_TEXT(ok)};
      chk_result_t said = chk_say(&fresh, 1);
      chk_release(c);
      return said == CHK_IDLE ? on_idle : on_exit;
    }

    // the decay is a run of its own, not a continuation of the baseline
    chk_measure_reset(&c->run);
    c->step = VENT_STEP_TRIGGER;
  }

  /* Trigger */

  if (c->step == VENT_STEP_TRIGGER) {
    al_buzzer_beep(1047, 80, false);
    const chk_bubble_t open = {
        &img_robin_pointing,
        lvx_fmt(CHK_TEXT(vent__open_window), c->result[CHK_VENT_C0]),
        CHK_TEXT(vent__window_is_open),
    };
    VENT_TRY(chk_say(&open, 1));

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
  VENT_TRY(chk_measure(c, &decay, self));
    c->step = VENT_STEP_RESULT;
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
    chk_result_t said = chk_say(&unclear, 1);
    chk_release(c);
    if (said == CHK_IDLE) return on_idle;
    if (said == CHK_NEXT) return self;
    return on_exit;
  }

  // seal it before saying anything: walking away from the verdict should not
  // lose the check. A result step re-entered afterwards gets the same record.
  uint16_t stored = chk_record(c, AL_SAMPLE_CO2);

  // the verdict: the one number, and the advice the tier earns
  int half = chk_round_minutes(c->result[CHK_VENT_HALF_LIFE]);
  chk_vent_tier_t tier = chk_vent_tier(c->result[CHK_VENT_ACH]);
  const char *advice = tier == CHK_VENT_TIER_LOW    ? CHK_TEXT(vent__advice_low)
                       : tier == CHK_VENT_TIER_MID  ? CHK_TEXT(vent__advice_mid)
                                                    : CHK_TEXT(vent__advice_high);
  const chk_bubble_t verdict[] = {
      {tier == CHK_VENT_TIER_HIGH ? &img_robin_happy : &img_robin_standing,
       lvx_fmt(CHK_TEXT(vent__verdict_half_life), half), CHK_TEXT(next)},
      {&img_robin_pointing,
       advice, CHK_TEXT(next)},
  };
  VENT_TRY(chk_say(verdict, 2));

  /* Stats and share */

  // the same view a stored check is reopened through, so a result shown now
  // and the same result shown next week cannot say different things
  // read back what was just written, so the result shown now is built from
  // exactly the record a reopened check will be built from later
  chk_view_t view;
  if (!chk_view_of(stored, &view) &&
      !chk_describe(CHK_VENT, c->result, c->marks, CHK_MARKS, (uint8_t)chk_cadence(), &view)) {
    return on_exit;
  }
  VENT_TRY(chk_stats(view.title, CHK_TEXT(stage__results), view.lines, view.num_lines, view.note));
  VENT_TRY(chk_show_code(stored));

#undef VENT_TRY

  chk_release(c);
  return on_exit;
}
