#ifndef CHK_H
#define CHK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <al/sample.h>

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
  uint8_t id;
  int64_t start;                // epoch ms
  uint8_t phase;                // current phase
  int32_t marks[CHK_MARKS];     // phase boundaries, ms since start
  chk_accum_t accum[CHK_PASSES];
  float result[CHK_RESULTS];    // evaluator outputs
} chk_t;

// The signals a check needs, as a mask over al_sample_field_t. The feature
// runs on both devices and they do not carry the same sensors: PM2.5 arrives
// with the BMV080 on Air Lab 2, so a check that needs it must not be offered
// on a device without one.
#define CHK_NEEDS(field) (1u << (field))
#define CHK_NEEDS_CO2 CHK_NEEDS(AL_SAMPLE_CO2)
#define CHK_NEEDS_PM CHK_NEEDS(AL_SAMPLE_PM)

// Reports whether the device in hand can run a check with these needs.
bool chk_available(uint16_t needs);

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

// What a check makes of one sample.
typedef enum {
  CHK_STEP_WAIT,  // taken, but nothing is moving yet
  CHK_STEP_GO,    // taken, and the signal is moving
  CHK_STEP_DONE,  // the check has what it needs
} chk_step_t;

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

// Clears a run.
void chk_measure_reset(chk_measure_run_t *r);

// Folds one reading into a run and says what to do next. `valid` is whether
// the sensor gave a usable number; `verdict` is what the check made of it,
// and is ignored when it did not.
chk_run_state_t chk_measure_step(const chk_measure_cfg_t *cfg, chk_measure_run_t *r, int32_t elapsed, bool valid,
                                 chk_step_t verdict);

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
