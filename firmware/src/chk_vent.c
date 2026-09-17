#include <math.h>
#include <string.h>

#include <al/store.h>

#include "chk_vent.h"
#include "dev.h"

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

    return CHK_VENT_SOLID;
  }

  // no number worth reporting: fall back to the direction the air moved,
  // which the user can still act on
  c->result[CHK_VENT_ACH] = 0;
  c->result[CHK_VENT_ACH_BAND] = 0;
  c->result[CHK_VENT_R2] = solved ? r2 : 0;

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

// The remembered floor. RTC-retained like the check context, and cleared the
// same way: a reset that is not a deep sleep starts it empty.
typedef struct {
  float ppm;   // the floor, 0 while nothing is remembered
  int64_t at;  // epoch ms it was measured
  bool rough;  // it was still drifting at the cap
} chk_vent_outdoor_t;

DEV_KEEP static chk_vent_outdoor_t chk_vent_outdoor;

void chk_vent_outdoor_set(float ppm, bool rough, int64_t now) {
  chk_vent_outdoor.ppm = ppm;
  chk_vent_outdoor.at = now;
  chk_vent_outdoor.rough = rough;
}

void chk_vent_outdoor_forget(void) {
  memset(&chk_vent_outdoor, 0, sizeof(chk_vent_outdoor));
}

chk_vent_cout_how_t chk_vent_outdoor_get(int64_t now, float *ppm, float *age_hours) {
  // nothing remembered, or a value from a clock that has since been set back,
  // which is no age at all
  int64_t age = now - chk_vent_outdoor.at;
  if (chk_vent_outdoor.ppm <= 0 || age < 0 || age > CHK_VENT_OUTDOOR_TTL_MS) {
    *ppm = CHK_VENT_OUTDOOR_DEFAULT;
    *age_hours = 0;
    return CHK_VENT_COUT_ASSUMED;
  }

  *ppm = chk_vent_outdoor.ppm;
  *age_hours = (float)(age / 3600000.0);
  return chk_vent_outdoor.rough ? CHK_VENT_COUT_ROUGH : CHK_VENT_COUT_MEASURED;
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
  return chk_median(values, have);
}
