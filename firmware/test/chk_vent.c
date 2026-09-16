#include <math.h>
#include <string.h>

#include <unity.h>

#include "chk.h"
#include "chk_vent.h"

extern bool shim_pm_present;

// Feeds a synthetic first-order decay into the evaluator, the way an airing
// looks: c0 falling towards cout at the given rate, sampled every step_s.
static void feed(chk_t* c, float c0, float cout, float ach, int n, int step_s, float noise) {
  memset(c, 0, sizeof(*c));
  c->result[CHK_VENT_COUT] = cout;
  c->result[CHK_VENT_C0] = c0;
  for (int i = 0; i < n; i++) {
    int32_t t = i * step_s * 1000;
    double excess = (c0 - cout) * exp(-ach * (t / 3600000.0));
    chk_vent_observe(c, (float)(cout + excess + ((i * 37) % 11 - 5) * noise), t);
  }
}

static void test_a_clean_airing_solves() {
  // the walkthrough's own example: 843 ppm falling towards 425 over six minutes
  chk_t c;
  feed(&c, 843, 425, 2.9f, 72, 5, 0.4f);
  TEST_ASSERT_EQUAL_INT(CHK_VENT_SOLID, chk_vent_evaluate(&c, 72 * 5 * 1000));

  TEST_ASSERT_FLOAT_WITHIN(0.1, 2.9, c.result[CHK_VENT_ACH]);
  TEST_ASSERT_TRUE(c.result[CHK_VENT_R2] > 0.99);
  TEST_ASSERT_TRUE(c.result[CHK_VENT_ACH_BAND] > 0);

  // and the figures the verdict is made of, which the walkthrough shows as
  // "clears half the stale air in 14 min"
  TEST_ASSERT_EQUAL_INT(15, chk_round_minutes(c.result[CHK_VENT_HALF_LIFE]));
  TEST_ASSERT_TRUE(c.result[CHK_VENT_FRESH] > c.result[CHK_VENT_HALF_LIFE]);
}

static void test_samples_near_the_floor_are_excluded() {
  chk_t c;
  memset(&c, 0, sizeof(c));
  c.result[CHK_VENT_COUT] = 420;
  c.result[CHK_VENT_C0] = 900;

  TEST_ASSERT_TRUE(chk_vent_observe(&c, 900, 0));
  TEST_ASSERT_FALSE(chk_vent_observe(&c, 425, 5000));   // inside the margin
  TEST_ASSERT_FALSE(chk_vent_observe(&c, 400, 10000));  // below the floor
  TEST_ASSERT_EQUAL_INT(1, c.accum[0].n);

  // the last reading is still kept: the result reports the span actually seen
  TEST_ASSERT_EQUAL_FLOAT(400, c.result[CHK_VENT_CLAST]);
}

static void test_a_weak_signal_is_gated_out() {
  // a 20 ppm span cannot support a rate, whatever the fit's r2 says
  chk_t c;
  feed(&c, 620, 600, 0.4f, 72, 5, 0.2f);
  TEST_ASSERT_EQUAL_INT(CHK_VENT_SLOW, chk_vent_evaluate(&c, 72 * 5 * 1000));

  // and nothing numeric is left behind for a screen to render
  TEST_ASSERT_EQUAL_FLOAT(0, c.result[CHK_VENT_ACH]);
  TEST_ASSERT_TRUE(c.result[CHK_VENT_HALF_LIFE] < 0);
}

static void test_a_steep_but_short_drop_reports_a_direction() {
  chk_t c;
  memset(&c, 0, sizeof(c));
  c.result[CHK_VENT_COUT] = 420;
  c.result[CHK_VENT_C0] = 1200;
  for (int i = 0; i < 6; i++) {
    chk_vent_observe(&c, 1200 - i * 40.0f, i * 5000);
  }

  // too few points to fit, but 240 ppm in 30 s is unmistakably quick
  TEST_ASSERT_EQUAL_INT(CHK_VENT_QUICK, chk_vent_evaluate(&c, 30000));
}

static void test_a_rising_signal_is_never_solid() {
  chk_t c;
  feed(&c, 500, 420, -2.0f, 72, 5, 0.2f);
  TEST_ASSERT_NOT_EQUAL(CHK_VENT_SOLID, chk_vent_evaluate(&c, 360000));
}

static void test_the_verdict_scale() {
  TEST_ASSERT_EQUAL_INT(CHK_VENT_TIER_HIGH, chk_vent_tier(7.0f));
  TEST_ASSERT_EQUAL_INT(CHK_VENT_TIER_HIGH, chk_vent_tier(CHK_VENT_TIER_GOOD));
  TEST_ASSERT_EQUAL_INT(CHK_VENT_TIER_MID, chk_vent_tier(2.9f));
  TEST_ASSERT_EQUAL_INT(CHK_VENT_TIER_MID, chk_vent_tier(CHK_VENT_TIER_FAIR));
  TEST_ASSERT_EQUAL_INT(CHK_VENT_TIER_LOW, chk_vent_tier(1.9f));
}

static void test_decay_quantities() {
  // ln(2) / 2.9 * 60 and ln(20) / 2.9 * 60
  TEST_ASSERT_FLOAT_WITHIN(0.1, 14.34, chk_half_life(2.9f));
  TEST_ASSERT_FLOAT_WITHIN(0.2, 61.97, chk_fresh_time(2.9f));

  // a rate that is not a decay has neither
  TEST_ASSERT_TRUE(chk_half_life(0) < 0);
  TEST_ASSERT_TRUE(chk_fresh_time(-1) < 0);

  // the same error in the rate is worth more minutes when the room is slow
  TEST_ASSERT_TRUE(chk_half_life_band(1.0f, 0.3f) > chk_half_life_band(6.0f, 0.3f));

  TEST_ASSERT_EQUAL_INT(1, chk_round_minutes(0.3f));  // never "0 min"
  TEST_ASSERT_EQUAL_INT(3, chk_round_minutes(3.2f));
  TEST_ASSERT_EQUAL_INT(15, chk_round_minutes(14.34f));
  TEST_ASSERT_EQUAL_INT(60, chk_round_minutes(62.0f));
}

static void test_a_pm_check_is_hidden_without_the_sensor() {
  shim_pm_present = false;
  TEST_ASSERT_TRUE(chk_available(CHK_NEEDS_CO2));
  TEST_ASSERT_FALSE(chk_available(CHK_NEEDS_CO2 | CHK_NEEDS_PM));

  // on Air Lab 2 the same check is offered
  shim_pm_present = true;
  TEST_ASSERT_TRUE(chk_available(CHK_NEEDS_CO2 | CHK_NEEDS_PM));
  shim_pm_present = false;
}

void suite_chk_vent() {
  RUN_TEST(test_a_clean_airing_solves);
  RUN_TEST(test_samples_near_the_floor_are_excluded);
  RUN_TEST(test_a_weak_signal_is_gated_out);
  RUN_TEST(test_a_steep_but_short_drop_reports_a_direction);
  RUN_TEST(test_a_rising_signal_is_never_solid);
  RUN_TEST(test_the_verdict_scale);
  RUN_TEST(test_decay_quantities);
  RUN_TEST(test_a_pm_check_is_hidden_without_the_sensor);
}
