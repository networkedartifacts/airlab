#include <math.h>

#include "chk_stove.h"

bool chk_stove_observe_burn(chk_t *c, int pass, float co2, int32_t t_ms) {
  // keep the peak whatever the sample does to the fit, since the abort and
  // the result both report what the room actually reached
  if (co2 > c->result[CHK_STOVE_PEAK]) {
    c->result[CHK_STOVE_PEAK] = co2;
  }

  // only the opening window carries a straight rise
  if (t_ms > CHK_STOVE_SLOPE_MS) {
    return false;
  }

  // ppm against minutes, whose slope is the rise rate
  chk_accum_add(&c->accum[pass], t_ms / 60000.0, co2);

  return true;
}

bool chk_stove_observe_clear(chk_t *c, float co2, int32_t t_ms) {
  // the room falls back towards the baseline it started from
  float excess = co2 - c->result[CHK_STOVE_C0];
  if (!(excess > 10.0f)) {
    return false;
  }

  // ln of the excess against hours, whose slope is minus the air change rate
  chk_accum_add(&c->accum[CHK_STOVE_PASS_CLEAR], t_ms / 3600000.0, log(excess));

  return true;
}

chk_stove_quality_t chk_stove_evaluate(chk_t *c) {
  // solve both rises, which need rather fewer points than a decay because a
  // straight line through a strong signal is an easier thing to fit
  float slope1, r2_1, slope3, r2_3;
  bool got1 = chk_accum_fit(&c->accum[CHK_STOVE_PASS_OPEN], 6, &slope1, &r2_1);
  bool got3 = chk_accum_fit(&c->accum[CHK_STOVE_PASS_HOOD], 6, &slope3, &r2_3);

  // the clearing decay gives the hood's own air change rate, and is reported
  // whether or not the capture ratio works out
  float decay, r2_decay;
  if (chk_accum_fit(&c->accum[CHK_STOVE_PASS_CLEAR], 8, &decay, &r2_decay) && decay < 0) {
    c->result[CHK_STOVE_HOOD_ACH] = -decay;
  } else {
    c->result[CHK_STOVE_HOOD_ACH] = -1;
  }

  // a ratio needs both rises, and a hood-off pass that actually rose: without
  // a burner raising the room there is nothing for the hood to catch
  if (!got1 || !got3 || slope1 <= 0 || r2_1 < CHK_STOVE_GATE_R2 || r2_3 < CHK_STOVE_GATE_R2) {
    c->result[CHK_STOVE_CAPTURE] = -1;
    c->result[CHK_STOVE_R2] = 0;
    return CHK_STOVE_UNCLEAR;
  }

  // a hood that made the room rise faster has not captured anything; report
  // nothing caught rather than a negative share
  float capture = 1.0f - slope3 / slope1;
  if (capture < 0) {
    capture = 0;
  } else if (capture > 1) {
    capture = 1;
  }

  c->result[CHK_STOVE_SLOPE1] = slope1;
  c->result[CHK_STOVE_SLOPE3] = slope3;
  c->result[CHK_STOVE_CAPTURE] = capture;
  c->result[CHK_STOVE_R2] = r2_1 < r2_3 ? r2_1 : r2_3;

  return CHK_STOVE_SOLID;
}

chk_stove_tier_t chk_stove_tier(float capture) {
  if (capture >= CHK_STOVE_TIER_GOOD) {
    return CHK_STOVE_TIER_HIGH;
  } else if (capture >= CHK_STOVE_TIER_FAIR) {
    return CHK_STOVE_TIER_MID;
  }
  return CHK_STOVE_TIER_LOW;
}
