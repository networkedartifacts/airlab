#include <math.h>
#include <string.h>

#include <unity.h>

#include "chk.h"
#include "chk_bath.h"

// Feeds a synthetic recovery into the evaluator, the way an open window
// looks: the excess over the baseline falling at the given rate, sampled
// every step_s seconds.
static void feed(chk_t* c, float rh0, float peak, float k, int n, int step_s, float noise) {
  memset(c, 0, sizeof(*c));
  c->result[CHK_BATH_RH0] = rh0;
  c->result[CHK_BATH_PEAK] = peak;
  for (int i = 0; i < n; i++) {
    int32_t t = i * step_s * 1000;
    double excess = (peak - rh0) * exp(-k * (t / 3600000.0));
    chk_bath_observe(c, (float)(rh0 + excess + ((i * 37) % 11 - 5) * noise), t);
  }
}

static void test_a_clean_airing_solves() {
  // 85 % falling back towards a 48 % bathroom over twelve minutes, which is
  // an open window in a small room
  chk_t c;
  feed(&c, 48, 85, 6.0f, 72, 10, 0.05f);
  TEST_ASSERT_EQUAL_INT(CHK_BATH_SOLID, chk_bath_evaluate(&c, 72 * 10 * 1000));

  TEST_ASSERT_FLOAT_WITHIN(0.2, 6.0, c.result[CHK_BATH_K]);
  TEST_ASSERT_TRUE(c.result[CHK_BATH_R2] > 0.99);
  TEST_ASSERT_TRUE(c.result[CHK_BATH_K_BAND] > 0);

  // the figure the verdict is made of follows from the rate: ln(2) / 6 of an
  // hour is seven minutes
  TEST_ASSERT_EQUAL_INT(7, chk_round_minutes(chk_half_life(c.result[CHK_BATH_K])));
  TEST_ASSERT_EQUAL_INT(CHK_BATH_TIER_HIGH, chk_bath_tier(chk_half_life(c.result[CHK_BATH_K])));
}

static void test_the_peak_is_tracked_through_the_recovery() {
  // humidity often keeps climbing for a minute after the water stops, so the
  // peak is whatever was actually seen, not whatever the shower ended on
  chk_t c;
  memset(&c, 0, sizeof(c));
  c.result[CHK_BATH_RH0] = 48;
  c.result[CHK_BATH_PEAK] = 48;

  chk_bath_peak(&c, 76);
  TEST_ASSERT_EQUAL_FLOAT(76, c.result[CHK_BATH_PEAK]);

  chk_bath_observe(&c, 81, 0);
  TEST_ASSERT_EQUAL_FLOAT(81, c.result[CHK_BATH_PEAK]);

  chk_bath_observe(&c, 70, 30000);
  TEST_ASSERT_EQUAL_FLOAT(81, c.result[CHK_BATH_PEAK]);
  TEST_ASSERT_EQUAL_FLOAT(70, c.result[CHK_BATH_RH1]);
}

static void test_samples_near_the_baseline_are_excluded() {
  chk_t c;
  memset(&c, 0, sizeof(c));
  c.result[CHK_BATH_RH0] = 48;
  c.result[CHK_BATH_PEAK] = 85;

  TEST_ASSERT_TRUE(chk_bath_observe(&c, 85, 0));
  TEST_ASSERT_FALSE(chk_bath_observe(&c, 48.6f, 10000));  // inside the margin
  TEST_ASSERT_FALSE(chk_bath_observe(&c, 47, 20000));     // below the baseline
  TEST_ASSERT_EQUAL_INT(1, c.accum[0].n);

  // the last reading is still kept: the result reports the span actually seen
  TEST_ASSERT_EQUAL_FLOAT(47, c.result[CHK_BATH_RH1]);
}

static void test_a_bathroom_that_barely_dries_reports_a_direction() {
  // two per cent over half an hour is no fit worth a number, and the
  // direction is what the user can act on
  chk_t c;
  feed(&c, 48, 51, 0.15f, 60, 30, 0.02f);
  TEST_ASSERT_EQUAL_INT(CHK_BATH_SLOW, chk_bath_evaluate(&c, 30 * 60 * 1000));

  // and nothing numeric is left behind for a screen to render
  TEST_ASSERT_EQUAL_FLOAT(0, c.result[CHK_BATH_K]);
  TEST_ASSERT_TRUE(chk_half_life(c.result[CHK_BATH_K]) < 0);
  TEST_ASSERT_EQUAL_INT(CHK_BATH_TIER_LOW, chk_bath_tier(chk_half_life(c.result[CHK_BATH_K])));
}

static void test_a_steep_but_short_drop_reports_a_direction() {
  chk_t c;
  memset(&c, 0, sizeof(c));
  c.result[CHK_BATH_RH0] = 48;
  c.result[CHK_BATH_PEAK] = 85;
  for (int i = 0; i < 6; i++) {
    chk_bath_observe(&c, 85 - i * 2.0f, i * 10000);
  }

  // too few points to fit, but 10 % in a minute is unmistakably quick
  TEST_ASSERT_EQUAL_INT(CHK_BATH_QUICK, chk_bath_evaluate(&c, 60000));
}

static void test_a_rising_signal_is_never_solid() {
  // the window was never opened, or the shower was still running
  chk_t c;
  feed(&c, 48, 55, -3.0f, 60, 10, 0.02f);
  TEST_ASSERT_NOT_EQUAL(CHK_BATH_SOLID, chk_bath_evaluate(&c, 600000));
}

static void test_the_recovery_counts_down_to_where_it_ends() {
  // the run ends once four fifths of the excess is gone; at half the excess
  // and 6 per hour that is ln(0.5 / 0.2) / 6 of an hour away
  chk_t c;
  feed(&c, 48, 85, 6.0f, 42, 10, 0);
  TEST_ASSERT_INT32_WITHIN(20000, 550000, chk_bath_remaining(&c, CHK_BATH_RECOVERY_SHARE));

  // the same recovery carried past that point has nothing left to wait for
  feed(&c, 48, 85, 6.0f, 120, 10, 0);
  TEST_ASSERT_EQUAL_INT32(0, chk_bath_remaining(&c, CHK_BATH_RECOVERY_SHARE));

  // and until the fit solves there is nothing to say
  chk_t fresh;
  memset(&fresh, 0, sizeof(fresh));
  fresh.result[CHK_BATH_RH0] = 48;
  fresh.result[CHK_BATH_PEAK] = 85;
  for (int i = 0; i < 4; i++) {
    chk_bath_observe(&fresh, 85 - i * 0.5f, i * 10000);
  }
  TEST_ASSERT_EQUAL_INT32(-1, chk_bath_remaining(&fresh, CHK_BATH_RECOVERY_SHARE));
}

static void test_the_verdict_scale() {
  // the half-life in minutes, a design choice with no source behind it
  TEST_ASSERT_EQUAL_INT(CHK_BATH_TIER_HIGH, chk_bath_tier(4));
  TEST_ASSERT_EQUAL_INT(CHK_BATH_TIER_HIGH, chk_bath_tier(CHK_BATH_TIER_GOOD));
  TEST_ASSERT_EQUAL_INT(CHK_BATH_TIER_MID, chk_bath_tier(12));
  TEST_ASSERT_EQUAL_INT(CHK_BATH_TIER_MID, chk_bath_tier(CHK_BATH_TIER_FAIR));
  TEST_ASSERT_EQUAL_INT(CHK_BATH_TIER_LOW, chk_bath_tier(35));

  // a rate that never solved has no half-life at all
  TEST_ASSERT_EQUAL_INT(CHK_BATH_TIER_LOW, chk_bath_tier(-1));
}

// an hour in ms, and a day
#define HOUR (3600 * 1000LL)
#define DAY (24 * HOUR)

static void test_outdoor_air_is_remembered_with_its_age() {
  chk_bath_outdoor_forget();

  // nothing remembered: the step has only measuring or skipping to offer
  float tmp = 0, rh = 0, age = 0;
  TEST_ASSERT_FALSE(chk_bath_outdoor_get(1000 * DAY, &tmp, &rh, &age));

  int64_t measured = 1000 * DAY;
  chk_bath_outdoor_set(8, 62, measured);
  TEST_ASSERT_TRUE(chk_bath_outdoor_get(measured + 2 * HOUR, &tmp, &rh, &age));
  TEST_ASSERT_EQUAL_FLOAT(8, tmp);
  TEST_ASSERT_EQUAL_FLOAT(62, rh);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 2, age);

  chk_bath_outdoor_forget();
}

static void test_outdoor_air_expires_as_the_weather_moves() {
  chk_bath_outdoor_forget();
  int64_t measured = 1000 * DAY;
  chk_bath_outdoor_set(8, 62, measured);

  // still good just inside three hours
  float tmp = 0, rh = 0, age = 0;
  TEST_ASSERT_TRUE(chk_bath_outdoor_get(measured + 3 * HOUR - 1, &tmp, &rh, &age));

  // and gone beyond it, or if the clock went backwards
  TEST_ASSERT_FALSE(chk_bath_outdoor_get(measured + 3 * HOUR + 1, &tmp, &rh, &age));
  TEST_ASSERT_FALSE(chk_bath_outdoor_get(measured - 1, &tmp, &rh, &age));

  chk_bath_outdoor_forget();
}

void suite_chk_bath() {
  RUN_TEST(test_a_clean_airing_solves);
  RUN_TEST(test_the_peak_is_tracked_through_the_recovery);
  RUN_TEST(test_samples_near_the_baseline_are_excluded);
  RUN_TEST(test_a_bathroom_that_barely_dries_reports_a_direction);
  RUN_TEST(test_a_steep_but_short_drop_reports_a_direction);
  RUN_TEST(test_a_rising_signal_is_never_solid);
  RUN_TEST(test_the_recovery_counts_down_to_where_it_ends);
  RUN_TEST(test_the_verdict_scale);
  RUN_TEST(test_outdoor_air_is_remembered_with_its_age);
  RUN_TEST(test_outdoor_air_expires_as_the_weather_moves);
}
