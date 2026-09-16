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
