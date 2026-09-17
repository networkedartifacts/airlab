#include <math.h>
#include <string.h>

#include <al/store.h>

#include "chk_bath.h"
#include "dev.h"

void chk_bath_peak(chk_t *c, float rh) {
  if (rh > c->result[CHK_BATH_PEAK]) {
    c->result[CHK_BATH_PEAK] = rh;
  }
}

bool chk_bath_observe(chk_t *c, float rh, int32_t t_ms) {
  // keep the latest reading whatever happens to it, and keep watching the
  // peak: humidity often climbs for a minute after the water stops
  c->result[CHK_BATH_RH1] = rh;
  chk_bath_peak(c, rh);

  // ignore samples too close to the baseline: the logarithm of a small excess
  // is dominated by sensor noise, and a negative one is undefined
  float excess = rh - c->result[CHK_BATH_RH0];
  if (!(excess > CHK_BATH_LOG_MARGIN)) {
    return false;
  }

  // accumulate ln(RH - RH0) against time in hours, whose slope is -k
  chk_accum_add(&c->accum[0], t_ms / 3600000.0, log(excess));

  return true;
}

int32_t chk_bath_remaining(chk_t *c, float share) {
  // the slope is the rate, and until the fit solves there is nothing to say.
  // A rising or flat signal is not a recovery and has no end to count towards
  float slope, r2;
  if (!chk_accum_fit(&c->accum[0], CHK_BATH_FIT_MIN, &slope, &r2) || slope >= 0) {
    return -1;
  }
  float k = -slope;

  // where the run ends, and where it stands now, both as an excess over the
  // baseline the moisture falls back towards
  float target = (1.0f - share) * (c->result[CHK_BATH_PEAK] - c->result[CHK_BATH_RH0]);
  float excess = c->result[CHK_BATH_RH1] - c->result[CHK_BATH_RH0];
  if (!(target > 0) || !(excess > target)) {
    return 0;
  }

  // the excess decays at the fitted rate, so the time to fall to the target
  // is the log of the ratio over that rate, which is per hour
  return (int32_t)(logf(excess / target) / k * 3600000.0f);
}

chk_bath_quality_t chk_bath_evaluate(chk_t *c, int32_t elapsed_ms) {
  float drop = c->result[CHK_BATH_PEAK] - c->result[CHK_BATH_RH1];

  // solve the fit, needing enough points for the slope to mean anything
  float slope, r2;
  bool solved = chk_accum_fit(&c->accum[0], CHK_BATH_FIT_MIN, &slope, &r2);

  // a rising or flat signal is not a recovery, whatever the fit says
  if (solved && slope < 0 && r2 >= CHK_BATH_GATE_R2 && drop >= CHK_BATH_GATE_DROP) {
    float band = 0;
    chk_accum_stderr(&c->accum[0], &band);

    c->result[CHK_BATH_K] = -slope;
    c->result[CHK_BATH_K_BAND] = band;
    c->result[CHK_BATH_R2] = r2;

    return CHK_BATH_SOLID;
  }

  // no number worth reporting: fall back to the direction the moisture went,
  // which the user can still act on
  c->result[CHK_BATH_K] = 0;
  c->result[CHK_BATH_K_BAND] = 0;
  c->result[CHK_BATH_R2] = solved ? r2 : 0;

  // half a per cent a minute is this branch's own cut between "quickly" and
  // "slowly", the humidity counterpart of the ventilation check's 10 ppm
  float minutes = elapsed_ms / 60000.0f;
  if (minutes > 0 && drop / minutes >= 0.5f) {
    return CHK_BATH_QUICK;
  }

  return CHK_BATH_SLOW;
}

chk_bath_tier_t chk_bath_tier(float half_life) {
  // a rate that did not solve has no half-life, and lands in the lowest step
  if (half_life < 0) {
    return CHK_BATH_TIER_LOW;
  }
  if (half_life <= CHK_BATH_TIER_GOOD) {
    return CHK_BATH_TIER_HIGH;
  } else if (half_life <= CHK_BATH_TIER_FAIR) {
    return CHK_BATH_TIER_MID;
  }
  return CHK_BATH_TIER_LOW;
}

// The remembered outdoor air. RTC-retained like the ventilation check's floor
// and cleared the same way: a reset that is not a deep sleep starts it empty.
typedef struct {
  float tmp;   // the temperature outside (C)
  float rh;    // and the humidity (%), 0 while nothing is remembered
  int64_t at;  // epoch ms it was measured
} chk_bath_outdoor_t;

DEV_KEEP static chk_bath_outdoor_t chk_bath_outdoor;

void chk_bath_outdoor_set(float tmp, float rh, int64_t now) {
  chk_bath_outdoor.tmp = tmp;
  chk_bath_outdoor.rh = rh;
  chk_bath_outdoor.at = now;
}

void chk_bath_outdoor_forget(void) {
  memset(&chk_bath_outdoor, 0, sizeof(chk_bath_outdoor));
}

bool chk_bath_outdoor_get(int64_t now, float *tmp, float *rh, float *age_hours) {
  // nothing remembered, or a value from a clock that has since been set back,
  // which is no age at all
  int64_t age = now - chk_bath_outdoor.at;
  if (chk_bath_outdoor.rh <= 0 || age < 0 || age > CHK_BATH_OUTDOOR_TTL_MS) {
    return false;
  }

  *tmp = chk_bath_outdoor.tmp;
  *rh = chk_bath_outdoor.rh;
  *age_hours = (float)(age / 3600000.0);

  return true;
}

float chk_bath_median(al_sample_field_t field, int n) {
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
    float value = al_sample_read(sample, field);
    if (!isnan(value)) {
      values[have++] = value;
    }
  }
  return chk_median(values, have);
}
