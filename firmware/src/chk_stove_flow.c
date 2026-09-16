// The gas stove check as the user walks through it. Three passes with a pot
// of water; the only structural difference from the ventilation check is that
// the middle of it is a loop.

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include <al/buzzer.h>
#include <al/store.h>

#include "chk.h"
#include "chk_stove.h"
#include "chk_vent.h"
#include "gui.h"
#include "img.h"
#include "lvx.h"

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
  const char* prompt;
  const char* action;
  const char* stage;
  bool burning;
} chk_stove_pass_t;

static chk_step_t stove_sample(chk_t* c, float value, int32_t t_ms) {
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

static chk_step_t stove_baseline_sample(chk_t* c, float value, int32_t t_ms) {
  (void)c;
  (void)value;
  (void)t_ms;
  return CHK_STEP_WAIT;
}

void* chk_stove_run(void* on_exit, void* on_idle, void* self) {
  // pick the check up where it stopped, or start it
  chk_t* c = chk_context();
  if (!chk_resuming(c, CHK_STOVE)) {
    chk_begin(c, CHK_STOVE);
  }

  const char* title = CHK_TEXT(stove__title);

// Leaving and timing out are not the same thing. A user who presses escape
// has abandoned the check, so the context is released and the next run starts
// over. A timeout is the device going to sleep with the user still intending
// to finish, so the context is kept and the flow resumes into the step it
// was on.
#define STOVE_TRY(expr)                 \
  do {                                  \
    chk_result_t _r = (expr);           \
    if (_r == CHK_EXIT) {               \
      chk_release(c);                       \
      return on_exit;                   \
    }                                   \
    if (_r == CHK_IDLE) return on_idle; \
    if (_r == CHK_AGAIN) {              \
      chk_release(c);                       \
      return self;                      \
    }                                   \
  } while (0)

  /* Introduction */

  if (c->step == STOVE_STEP_INTRO) {
    const chk_bubble_t intro[] = {
        {&img_robin_happy, CHK_TEXT(stove__intro_1), CHK_TEXT(next)},
        {&img_robin_pointing, CHK_TEXT(stove__intro_2), CHK_TEXT(next)},
        {&img_robin_standing, CHK_TEXT(stove__intro_3), CHK_TEXT(start)},
    };
    STOVE_TRY(chk_say(intro, sizeof(intro) / sizeof(intro[0])));
    c->step = STOVE_STEP_PRECHECK;
  }

  /* Precheck */

  if (c->step == STOVE_STEP_PRECHECK) {
    const char* const items[] = {
        CHK_TEXT(stove__list_off),
        CHK_TEXT(stove__list_pot),
        CHK_TEXT(stove__list_away),
    };
    STOVE_TRY(chk_list(title, CHK_TEXT(stage__baseline), items, 3));
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
      .on_sample = stove_baseline_sample,
  };
  STOVE_TRY(chk_measure(c, &baseline, self));

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
    const chk_stove_pass_t* pass = &passes[i];

    // the callback reads the pass back off the context, and a resume comes
    // back into the pass it left
    c->phase = (uint8_t)i;

    // the prompt, unless the pass is already under way: a resume into a
    // measurement must not ask for the burner again
    if (c->run.began == 0) {
      al_buzzer_beep(1047, 80, false);
      const chk_bubble_t prompt = {&img_robin_pointing, pass->prompt, pass->action};
      STOVE_TRY(chk_say(&prompt, 1));
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
    STOVE_TRY(chk_measure(c, &measure, self));

    // where this pass ended, so the page can band the chart per pass
    chk_mark(c);

    // the kitchen has had enough: stop the check rather than the pass
    if (c->result[CHK_STOVE_PEAK] >= CHK_STOVE_ABORT_PPM) {
      const chk_bubble_t stop = {&img_robin_angry1, CHK_TEXT(stove__too_much), CHK_TEXT(ok)};
      chk_result_t said = chk_say(&stop, 1);
      chk_release(c);
      return said == CHK_IDLE ? on_idle : on_exit;
    }

    // the next pass starts fresh
    chk_measure_reset(&c->run);
  }
  c->step = STOVE_STEP_RESULT;

  /* Result */

  if (chk_stove_evaluate(c) != CHK_STOVE_SOLID) {
    const chk_bubble_t unclear[] = {
        {&img_robin_standing, CHK_TEXT(stove__unclear_1), CHK_TEXT(next)},
        {&img_robin_standing, CHK_TEXT(stove__unclear_2), CHK_TEXT(again)},
    };
    chk_result_t said = chk_say(unclear, 2);
    chk_release(c);
    if (said == CHK_IDLE) return on_idle;
    if (said == CHK_NEXT) return self;
    return on_exit;
  }

  // seal it before saying anything, as the ventilation check does
  uint16_t stored = chk_record(c, AL_SAMPLE_CO2);

  int percent = (int)(c->result[CHK_STOVE_CAPTURE] * 100 + 0.5f);
  chk_stove_tier_t tier = chk_stove_tier(c->result[CHK_STOVE_CAPTURE]);
  const char* advice = tier == CHK_STOVE_TIER_LOW    ? CHK_TEXT(stove__advice_low)
                       : tier == CHK_STOVE_TIER_MID  ? CHK_TEXT(stove__advice_mid)
                                                     : CHK_TEXT(stove__advice_high);

  const chk_bubble_t verdict[] = {
      {tier == CHK_STOVE_TIER_HIGH ? &img_robin_happy : &img_robin_standing,
       lvx_fmt(CHK_TEXT(stove__verdict), percent), CHK_TEXT(next)},
      {&img_robin_pointing, advice, CHK_TEXT(next)},
  };
  STOVE_TRY(chk_say(verdict, 2));

  /* Stats */

  // the same view a stored check is reopened through
  // read back what was just written, so the result shown now is built from
  // exactly the record a reopened check will be built from later
  chk_view_t view;
  if (!chk_view_of(stored, &view) &&
      !chk_describe(CHK_STOVE, c->result, c->marks, CHK_MARKS, (uint8_t)chk_cadence(), &view)) {
    return on_exit;
  }
  STOVE_TRY(chk_stats(view.title, CHK_TEXT(stage__results), view.lines, view.num_lines, view.note));
  STOVE_TRY(chk_show_code(stored));

#undef STOVE_TRY

  chk_release(c);
  return on_exit;
}
