#include <math.h>

#include "chk_bedroom.h"

void chk_bedroom_reset(chk_t *c) {
  // the night starts from nothing: the accumulators, the levels and the
  // bands, which have no reading yet rather than a reading of zero
  for (int i = 0; i < CHK_PASSES; i++) {
    chk_accum_reset(&c->accum[i]);
  }
  c->result[CHK_BEDROOM_CMAX] = 0;
  c->result[CHK_BEDROOM_CMEAN] = 0;
  c->result[CHK_BEDROOM_C1] = 0;
  c->result[CHK_BEDROOM_OVER_LOW] = 0;
  c->result[CHK_BEDROOM_OVER_HIGH] = 0;
  c->result[CHK_BEDROOM_TMIN] = NAN;
  c->result[CHK_BEDROOM_TMAX] = NAN;
  c->result[CHK_BEDROOM_RHMIN] = NAN;
  c->result[CHK_BEDROOM_RHMAX] = NAN;
  c->result[CHK_BEDROOM_FLOW] = 0;
  c->result[CHK_BEDROOM_PLATEAU] = 0;
  c->result[CHK_BEDROOM_LAST_S] = -1;
}

// widens a band that may not have been opened yet, ignoring a reading the
// sensor could not give
static void chk_bedroom_band(float *lo, float *hi, float value) {
  if (isnan(value)) {
    return;
  }
  if (isnan(*lo) || value < *lo) {
    *lo = value;
  }
  if (isnan(*hi) || value > *hi) {
    *hi = value;
  }
}

void chk_bedroom_observe(chk_t *c, float co2, float tmp, float rh, int32_t t_ms) {
  float *r = c->result;

  // the newest reading is where the night ends, and the highest is its peak
  r[CHK_BEDROOM_C1] = co2;
  if (co2 > r[CHK_BEDROOM_CMAX]) {
    r[CHK_BEDROOM_CMAX] = co2;
  }

  // the stretch of night this reading stands for is the gap back to the one
  // before it: a gap too long for the check to have been watching counts for
  // nothing rather than for all of it
  float last = r[CHK_BEDROOM_LAST_S];
  float secs = t_ms / 1000.0f;
  float held = last >= 0 ? secs - last : 0;
  if (held < 0 || held * 1000 > CHK_BEDROOM_GAP_MAX_MS) {
    held = 0;
  }
  r[CHK_BEDROOM_LAST_S] = secs;
  if (co2 >= CHK_BEDROOM_LEVEL_LOW) {
    r[CHK_BEDROOM_OVER_LOW] += held / 3600.0f;
  }
  if (co2 >= CHK_BEDROOM_LEVEL_HIGH) {
    r[CHK_BEDROOM_OVER_HIGH] += held / 3600.0f;
  }

  // the temperature and humidity ride along as the bands the night stayed in
  chk_bedroom_band(&r[CHK_BEDROOM_TMIN], &r[CHK_BEDROOM_TMAX], tmp);
  chk_bedroom_band(&r[CHK_BEDROOM_RHMIN], &r[CHK_BEDROOM_RHMAX], rh);

  // the night's own sum, for the mean
  double hours = t_ms / 3600000.0;
  chk_accum_add(&c->accum[CHK_BEDROOM_FIT_NIGHT], hours, co2);

  // and the hour buckets behind it: a reading in a later hour than the one
  // before it closes the bucket and opens the next
  if (last >= 0 && (int)(last / 3600) != t_ms / 3600000) {
    c->accum[CHK_BEDROOM_FIT_PRIOR] = c->accum[CHK_BEDROOM_FIT_HOUR];
    chk_accum_reset(&c->accum[CHK_BEDROOM_FIT_HOUR]);
  }
  chk_accum_add(&c->accum[CHK_BEDROOM_FIT_HOUR], hours, co2);
}

float chk_bedroom_flow(float co2) {
  // the balance is undefined at or below the reference, where the room is as
  // fresh as the sensor's own floor
  float excess = co2 - CHK_BEDROOM_OUTDOOR;
  if (!(excess > 0)) {
    return 0;
  }

  // G / (C - Cout) with the difference in ppm, which is parts per million of
  // the same litres, so the generation is scaled by a million
  return CHK_BEDROOM_GEN_LPS * 1e6f / excess;
}

void chk_bedroom_evaluate(chk_t *c, int32_t elapsed_ms) {
  float *r = c->result;

  // the night's mean, out of the running sum
  const chk_accum_t *night = &c->accum[CHK_BEDROOM_FIT_NIGHT];
  r[CHK_BEDROOM_CMEAN] = night->n > 0 ? (float)(night->sy / night->n) : 0;

  // the last hour is the bucket being filled once it holds half an hour, and
  // the one before it while it does not, so the answer always rests on
  // between thirty and sixty minutes of readings
  const chk_accum_t *last = &c->accum[CHK_BEDROOM_FIT_HOUR];
  if (elapsed_ms % 3600000 < CHK_BEDROOM_HOUR_MIN_MS && c->accum[CHK_BEDROOM_FIT_PRIOR].n > 0) {
    last = &c->accum[CHK_BEDROOM_FIT_PRIOR];
  }
  float mean = last->n > 0 ? (float)(last->sy / last->n) : 0;

  // the outdoor air each sleeper got by morning, which a night too short to
  // have settled anywhere does not report at all
  r[CHK_BEDROOM_FLOW] = elapsed_ms >= CHK_BEDROOM_FLOW_MIN_MS ? chk_bedroom_flow(mean) : 0;

  // and whether the room had settled: the slope of the last hour is what it
  // moved across it, since the fit runs against hours
  float slope, r2;
  bool solved = chk_accum_fit(last, CHK_BEDROOM_FIT_MIN, &slope, &r2);
  bool held = solved ? fabsf(slope) < CHK_BEDROOM_PLATEAU_PPM
                     // an hour that did not move at all has no slope to fit,
                     // and is the plateau the fit would have found
                     : last->n >= CHK_BEDROOM_FIT_MIN && last->n * last->syy - last->sy * last->sy <= 0;
  r[CHK_BEDROOM_PLATEAU] = held ? 1 : 0;
}

chk_bedroom_tier_t chk_bedroom_tier(float cmean) {
  if (cmean < CHK_BEDROOM_TIER_GOOD) {
    return CHK_BEDROOM_TIER_HIGH;
  } else if (cmean < CHK_BEDROOM_TIER_FAIR) {
    return CHK_BEDROOM_TIER_MID;
  }
  return CHK_BEDROOM_TIER_LOW;
}
