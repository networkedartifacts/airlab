#include <math.h>

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
