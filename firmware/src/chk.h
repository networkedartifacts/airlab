#ifndef CHK_H
#define CHK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>

#include <al/sample.h>

#include "chk_code.h"

// The guided air checks. A check is a screen function that calls the kit
// below and returns an outcome; control flow stays ordinary C, because that
// is where the per-check variety lives.

#define CHK_MARKS 8    // phase boundaries per check
#define CHK_PASSES 3   // fitted segments per check (the stove check uses all)
#define CHK_RESULTS 8  // evaluator outputs per check

// What a kit call reports back. The caller maps these to screens; only
// CHK_NEXT continues the flow.
typedef enum {
  CHK_NEXT,   // carry on
  CHK_AGAIN,  // restart this check
  CHK_EXIT,   // user left
  CHK_IDLE,   // timed out
} chk_result_t;

// What a check makes of one sample.
typedef enum {
  CHK_STEP_WAIT,  // taken, but nothing is moving yet
  CHK_STEP_GO,    // taken, and the signal is moving
  CHK_STEP_DONE,  // the check has what it needs
} chk_step_t;

typedef struct {
  int32_t min_ms;    // the earliest a check may declare itself done
  int32_t max_ms;    // the latest the run may go on, 0 for no limit
  int capacity;      // samples the run may take, 0 for no limit
  int32_t nudge_ms;  // how long to wait before prompting, 0 to never
} chk_measure_cfg_t;

typedef struct {
  int attempts;      // readings asked for
  int fails;         // readings that came back unusable
  int count;         // readings that counted
  int32_t elapsed;   // ms since the run began
  bool nudging;      // the user should be prompted
} chk_measure_run_t;

// Least-squares terms accumulated one sample at a time. The context must fit
// in RTC-retained memory so a check survives deep sleep, which is why a check
// never holds its sample stream: the stream lives in the device's own stores
// and the fit is computed from these running sums.
typedef struct {
  int n;
  double sx;
  double sy;
  double sxx;
  double sxy;
  double syy;
} chk_accum_t;

// The in-flight state of a check. Retained across deep sleep, lost on a crash.
typedef struct {
  uint8_t id;                   // which check, or CHK_NONE when idle
  uint8_t step;                 // how far through the flow, for resuming
  int64_t start;                // epoch ms
  uint8_t phase;                // current phase within a step
  int32_t marks[CHK_MARKS];     // phase boundaries, ms since start
  chk_accum_t accum[CHK_PASSES];
  float result[CHK_RESULTS];    // evaluator outputs
  int64_t seen;                 // epoch of the last sample folded in
  chk_measure_run_t run;        // the run in progress
} chk_t;

// No check is in progress. A context holding this is free to be begun.
#define CHK_NONE 0xFF

// The check in progress, which lives in RTC-retained memory so that it
// survives a deep sleep. A deep sleep on this device is a reset: main memory
// is lost, the flow is re-entered from the top, and `step` is what lets it
// pick up where it stopped. Nothing here survives a crash, by decision.
chk_t *chk_context(void);

// True when this context is already part-way through the given check, so the
// flow should resume rather than start over.
bool chk_resuming(const chk_t *c, uint8_t id);

typedef enum {
  CHK_DE,
  CHK_EN,
  CHK_ES,
  CHK_FR,
} chk_lang_t;

typedef struct {
  // shared across checks
  const char* next;
  const char* start;
  const char* ok;
  const char* again;
  const char* done;
  const char* check;
  const char* back;
  const char* sensor_errors;
  const char* no_checks;
  const char* past_checks;
  const char* stage__baseline;
  const char* stage__measuring;
  const char* stage__results;
  const char* stage__share;
  const char* share_scan;
  const char* share_failed;

  // ventilation
  const char* vent__title;
  const char* vent__intro_1;
  const char* vent__intro_2;
  const char* vent__intro_3;
  const char* vent__intro_4;
  const char* vent__list_windows;
  const char* vent__list_door;
  const char* vent__list_table;
  const char* vent__outdoor;
  const char* vent__already_fresh;
  const char* vent__nudge;
  const char* vent__quickly;
  const char* vent__slowly;
  const char* vent__advice_low;
  const char* vent__advice_mid;
  const char* vent__advice_high;
  const char* vent__baseline_hint;
  const char* vent__open_window;
  const char* vent__window_is_open;
  const char* vent__unclear;
  const char* vent__verdict_half_life;
  const char* vent__verdict_second_window;
  const char* vent__stat_ach;
  const char* vent__stat_half_life;
  const char* vent__stat_fresh;
  const char* vent__stat_co2;
  const char* vent__stat_note;

  // gas stove
  const char* stove__title;
  const char* stove__intro_1;
  const char* stove__intro_2;
  const char* stove__intro_3;
  const char* stove__list_off;
  const char* stove__list_pot;
  const char* stove__list_away;
  const char* stove__baseline_hint;
  const char* stove__stage_open;
  const char* stove__stage_clear;
  const char* stove__stage_hood;
  const char* stove__pass_open;
  const char* stove__pass_clear;
  const char* stove__pass_hood;
  const char* stove__burner_on;
  const char* stove__hood_on;
  const char* stove__nudge;
  const char* stove__too_much;
  const char* stove__unclear_1;
  const char* stove__unclear_2;
  const char* stove__verdict;
  const char* stove__advice_low;
  const char* stove__advice_mid;
  const char* stove__advice_high;
  const char* stove__stat_capture;
  const char* stove__stat_hood;
  const char* stove__stat_hood_none;
  const char* stove__stat_peak;
  const char* stove__stat_note;
} chk_trans_t;

// The checks themselves.
typedef enum {
  CHK_VENT,
  CHK_STOVE,
} chk_id_t;

// How long a check waits on a prompt before giving the device back. A check
// left standing on a table should not hold the screen awake indefinitely.
#define CHK_ACTION_TIMEOUT 60000

// One thing Robin says, with the sign under the A key.
typedef struct {
  const lv_img_dsc_t *mood;
  const char *text;
  const char *action;
} chk_bubble_t;

// Says a sequence of bubbles, one key press each, and returns once the last
// is acknowledged. The action of each bubble labels the key; a NULL action
// falls back to the shared "Next".
chk_result_t chk_say(const chk_bubble_t *bubbles, size_t count);

// Draws the header every non-dialogue check screen carries. Must be called
// between gfx_begin and gfx_end.
void chk_chrome(const char *title, const char *stage);

// Shows a checklist the user confirms before a check starts, so that the
// single-zone assumption the decay and rise methods rest on is actually met.
chk_result_t chk_list(const char *title, const char *stage, const char *const *items, size_t count);

// Draws the QR code a phone scans to carry the result away, with a short
// line beside it. Returns once acknowledged.
chk_result_t chk_qr(const char *title, char letter, const char *digits, const char *caption);

// Shows a run's result as labelled lines with a note beneath.
chk_result_t chk_stats(const char *title, const char *stage, const char *const *lines, size_t count,
                       const char *note);

// The signals a check needs, as a mask over al_sample_field_t. The feature
// runs on both devices and they do not carry the same sensors: PM2.5 arrives
// with the BMV080 on Air Lab 2, so a check that needs it must not be offered
// on a device without one.
#define CHK_NEEDS(field) (1u << (field))
#define CHK_NEEDS_CO2 CHK_NEEDS(AL_SAMPLE_CO2)
#define CHK_NEEDS_PM CHK_NEEDS(AL_SAMPLE_PM)

// Reports whether the device in hand can run a check with these needs.
bool chk_available(uint16_t needs);

// Begins a check: stamps the start time, from which every phase boundary and
// the recorded window are measured.
void chk_begin(chk_t *c, uint8_t id);

// Ends a check, freeing the context for the next one.
void chk_end(chk_t *c);

// Marks a phase boundary at the current moment. A check is not one contiguous
// window — the user is prompted between phases and takes as long as they take
// — so the boundaries have to be recorded as they happen rather than inferred
// from the durations afterwards.
void chk_mark(chk_t *c);

// Milliseconds since the check began.
int32_t chk_elapsed(const chk_t *c);

// Selects the language for check copy, using the same indices as scr_lang_t.
void chk_init(int lang);

// Returns a check string, falling back to English when the selected language
// has none. The offset is a byte offset into chk_trans_t, as produced by
// offsetof; CHK_TEXT wraps it.
const char *chk_text_at(size_t offset);
#define CHK_TEXT(field) chk_text_at(offsetof(chk_trans_t, field))

// Adds one observation to a least-squares accumulator.
void chk_accum_add(chk_accum_t *a, double x, double y);

// Resets an accumulator to empty.
void chk_accum_reset(chk_accum_t *a);

// Solves an accumulator for the slope of y over x and the coefficient of
// determination. Returns false when there are fewer than min_n observations
// or the terms are degenerate, in which case slope and r2 are untouched.
bool chk_accum_fit(const chk_accum_t *a, int min_n, float *slope, float *r2);

// Solves for the standard error of the slope, which is the band a result
// carries. Returns false under the same conditions as chk_accum_fit, and
// additionally when there are fewer than three observations.
bool chk_accum_stderr(const chk_accum_t *a, float *stderr_slope);

// The measurement engine covers a baseline and a measurement both: they are
// the same loop, differing only in when they stop. The drawing lives in the
// screen; what follows is the policy, kept separate so it can be tested.

// What the engine does next.
typedef enum {
  CHK_RUN_GO,      // keep sampling
  CHK_RUN_DONE,    // stop, the run stands
  CHK_RUN_FAILED,  // stop, too many readings were unusable
} chk_run_state_t;

// A run is abandoned once more than a quarter of the attempts have failed,
// and not before there have been enough attempts for that to mean anything.
#define CHK_FAIL_MIN_ATTEMPTS 8
#define CHK_FAIL_SHARE 4

// Clears a run.
void chk_measure_reset(chk_measure_run_t *r);

// Folds one reading into a run and says what to do next. `valid` is whether
// the sensor gave a usable number; `verdict` is what the check made of it,
// and is ignored when it did not.
chk_run_state_t chk_measure_step(const chk_measure_cfg_t *cfg, chk_measure_run_t *r, int32_t elapsed, bool valid,
                                 chk_step_t verdict);

// How a run shows itself: a baseline counts samples towards a target, a
// measurement draws the signal falling or rising over time.
typedef enum {
  CHK_SHOW_PROGRESS,
  CHK_SHOW_CHART,
} chk_show_t;

// What a check makes of each reading.
typedef chk_step_t (*chk_sample_fn)(chk_t *c, float value, int32_t t_ms);

typedef struct {
  const char *title;
  const char *stage;
  const char *hint;   // under the value, or NULL
  const char *nudge;  // shown instead once the run is prompting, or NULL
  const char *unit;   // "ppm", "ug/m3"
  chk_show_t show;
  al_sample_field_t field;  // which signal to read
  chk_measure_cfg_t cfg;
  chk_sample_fn on_sample;
  float floor;  // the value a zero-height bar stands for
  float range;  // the span the chart covers above the floor, 0 to size it
} chk_screen_t;

// Runs a measurement: draws the screen, samples at the device's cadence,
// feeds each reading to the check, and stops when the policy says so. The
// run is filled in as it goes, so the caller can see what happened.
chk_result_t chk_measure(chk_t *c, const chk_screen_t *screen);

// Runs the ventilation check. The three arguments are the screens each
// outcome lands on: leaving, timing out, and starting over.
void *chk_vent_run(void *on_exit, void *on_idle, void *self);

// Runs the gas stove check, the same way.
void *chk_stove_run(void *on_exit, void *on_idle, void *self);

// A finished check, rebuilt from its result block. The same view serves the
// live flow and a reopened one, so the two cannot drift apart.
typedef struct {
  const char *title;
  char letter;
  al_sample_field_t signal;
  const char *headline;  // the one number the verdict is about
  const char *lines[6];
  size_t num_lines;
  const char *note;
  float payload[CHK_CODE_MAX_FIELDS];
  size_t num_payload;
} chk_view_t;

// Fills a view from a check's result block. False for an unknown check.
bool chk_describe(uint8_t id, const float *result, const int32_t *marks, uint8_t marks_len,
                  uint8_t cadence, chk_view_t *out);

// Packs a finished check and shows its code. `fields` are the format's own
// values in its own order; the samples come from the device's own store, so
// the check does not have to have kept them.
// Writes a finished check to flash, taking the window it spanned out of the
// device's own stores. Called as soon as the result is evaluated rather than
// when it is shown, so that walking away from the verdict does not lose it.
// Returns the file number, or zero when there was nothing worth keeping.
uint16_t chk_record(const chk_t *c, al_sample_field_t signal);

// Fills a view from a stored check, using the cadence it was recorded at
// rather than whatever the device is set to now. A live flow describes its
// result this way too: having just written the check, it reads it back, so
// the result shown now and the same result reopened later cannot differ.
bool chk_view_of(uint16_t num, chk_view_t *out);

// Draws the code for a stored check, rebuilt from its header and samples.
chk_result_t chk_show_code(uint16_t num);

// Shows a stored check again: its stats, then its code. Nothing is re-run.
chk_result_t chk_reopen(uint16_t num);

// Converts a first-order decay rate in air changes per hour into the two
// figures Persily 1997 defines: the half-life of the stale air, and the time
// to 95 per cent fresh, which is three time constants. Both in minutes, and
// both meaningless for a rate at or below zero, which they report as -1.
float chk_half_life(float ach);
float chk_fresh_time(float ach);

// Propagates the band on a rate onto its half-life, since half-life is not
// linear in the rate. Minutes, or -1 when the rate is not positive.
float chk_half_life_band(float ach, float ach_band);

// Rounds a duration in minutes to something worth saying out loud: whole
// minutes below ten, five-minute steps below forty-five, ten above.
int chk_round_minutes(float minutes);

#endif  // CHK_H
