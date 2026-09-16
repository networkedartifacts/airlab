#include <math.h>

#include <al/store.h>

#include "chk_vent.h"

bool chk_vent_observe(chk_t *c, float co2, int32_t t_ms) {
  // keep the latest reading whatever happens to it, the result reports the
  // span the user actually saw
  c->result[CHK_VENT_CLAST] = co2;

  // ignore samples too close to the outdoor floor: the logarithm of a small
  // excess is dominated by sensor noise, and a negative one is undefined
  float excess = co2 - c->result[CHK_VENT_COUT];
  if (!(excess > CHK_VENT_LOG_MARGIN)) {
    return false;
  }

  // accumulate ln(C - C_out) against time in hours, whose slope is -ACH
  chk_accum_add(&c->accum[0], t_ms / 3600000.0, log(excess));

  return true;
}

chk_vent_quality_t chk_vent_evaluate(chk_t *c, int32_t elapsed_ms) {
  float drop = c->result[CHK_VENT_C0] - c->result[CHK_VENT_CLAST];

  // solve the fit, needing enough points for the slope to mean anything
  float slope, r2;
  bool solved = chk_accum_fit(&c->accum[0], 12, &slope, &r2);

  // a rising or flat signal is not a decay, whatever the fit says
  if (solved && slope < 0 && r2 >= CHK_VENT_GATE_R2 && drop >= CHK_VENT_GATE_DROP) {
    float ach = -slope;
    float band = 0;
    chk_accum_stderr(&c->accum[0], &band);

    c->result[CHK_VENT_ACH] = ach;
    c->result[CHK_VENT_ACH_BAND] = band;
    c->result[CHK_VENT_R2] = r2;
    c->result[CHK_VENT_HALF_LIFE] = chk_half_life(ach);
    c->result[CHK_VENT_FRESH] = chk_fresh_time(ach);

    return CHK_VENT_SOLID;
  }

  // no number worth reporting: fall back to the direction the air moved,
  // which the user can still act on
  c->result[CHK_VENT_ACH] = 0;
  c->result[CHK_VENT_ACH_BAND] = 0;
  c->result[CHK_VENT_R2] = solved ? r2 : 0;
  c->result[CHK_VENT_HALF_LIFE] = -1;
  c->result[CHK_VENT_FRESH] = -1;

  // 10 ppm a minute is the branch's own cut between "quickly" and "slowly"
  float minutes = elapsed_ms / 60000.0f;
  if (minutes > 0 && drop / minutes >= 10.0f) {
    return CHK_VENT_QUICK;
  }

  return CHK_VENT_SLOW;
}

chk_vent_tier_t chk_vent_tier(float ach) {
  if (ach >= CHK_VENT_TIER_GOOD) {
    return CHK_VENT_TIER_HIGH;
  } else if (ach >= CHK_VENT_TIER_FAIR) {
    return CHK_VENT_TIER_MID;
  }
  return CHK_VENT_TIER_LOW;
}

float chk_vent_outdoor_guess(void) {
  // walk the long store, which reaches back far enough to have seen the room
  // ventilated at least once
  float lowest = NAN;
  size_t count = al_store_count(AL_STORE_LONG);
  for (size_t i = 0; i < count; i++) {
    al_sample_t sample = al_store_get(AL_STORE_LONG, (int)i);
    if (!al_sample_valid(sample)) {
      continue;
    }
    float co2 = al_sample_read(sample, AL_SAMPLE_CO2);
    if (!isnan(co2) && (isnan(lowest) || co2 < lowest)) {
      lowest = co2;
    }
  }

  // outdoor air is not below 400 ppm, whatever the sensor says
  if (isnan(lowest) || lowest < 400) {
    return 400;
  }

  return lowest;
}

float chk_vent_baseline_median(int n) {
  // read the last n from the short store, newest first
  float values[16];
  if (n > (int)(sizeof(values) / sizeof(values[0]))) {
    n = (int)(sizeof(values) / sizeof(values[0]));
  }
  int have = 0;
  for (int i = 0; i < n; i++) {
    al_sample_t sample = al_store_get(AL_STORE_SHORT, -1 - i);
    if (!al_sample_valid(sample)) {
      continue;
    }
    float co2 = al_sample_read(sample, AL_SAMPLE_CO2);
    if (!isnan(co2)) {
      values[have++] = co2;
    }
  }
  if (have == 0) {
    return NAN;
  }

  // insertion sort, which is plenty for a handful of readings
  for (int i = 1; i < have; i++) {
    for (int j = i; j > 0 && values[j] < values[j - 1]; j--) {
      float tmp = values[j];
      values[j] = values[j - 1];
      values[j - 1] = tmp;
    }
  }

  // the middle, or the mean of the two middles
  if (have % 2 == 1) {
    return values[have / 2];
  }
  return (values[have / 2 - 1] + values[have / 2]) / 2;
}
