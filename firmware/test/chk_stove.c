#include <math.h>
#include <string.h>

#include <unity.h>

#include "chk.h"
#include "chk_stove.h"

// Feeds a burn pass: the room rises from the baseline at the given rate, and
// the hood takes its share of the exhaust before it reaches the room.
static void burn(chk_t* c, int pass, float ppm_per_min, int n, int step_s) {
  for (int i = 0; i < n; i++) {
    int32_t t = i * step_s * 1000;
    float co2 = c->result[CHK_STOVE_C0] + ppm_per_min * (t / 60000.0f);
    chk_stove_observe_burn(c, pass, co2, t);
  }
}

static void clear(chk_t* c, float from, float ach, int n, int step_s) {
  for (int i = 0; i < n; i++) {
    int32_t t = i * step_s * 1000;
    float excess = (from - c->result[CHK_STOVE_C0]) * expf(-ach * (t / 3600000.0f));
    chk_stove_observe_clear(c, c->result[CHK_STOVE_C0] + excess, t);
  }
}

static void reset(chk_t* c, float baseline) {
  memset(c, 0, sizeof(*c));
  c->result[CHK_STOVE_C0] = baseline;
  c->result[CHK_STOVE_PEAK] = baseline;
}

static void test_a_good_hood_catches_most_of_it() {
  // 200 ppm a minute with the hood off, 60 with it on: it caught 70 per cent
  chk_t c;
  reset(&c, 612);
  burn(&c, CHK_STOVE_PASS_OPEN, 200, 24, 5);
  burn(&c, CHK_STOVE_PASS_HOOD, 60, 24, 5);

  TEST_ASSERT_EQUAL_INT(CHK_STOVE_SOLID, chk_stove_evaluate(&c));
  TEST_ASSERT_FLOAT_WITHIN(0.02, 0.70, c.result[CHK_STOVE_CAPTURE]);
  TEST_ASSERT_EQUAL_INT(CHK_STOVE_TIER_HIGH, chk_stove_tier(c.result[CHK_STOVE_CAPTURE]));
}

static void test_a_poor_hood_is_the_low_tier() {
  // LBNL puts installed hoods at about 55 per cent, so 30 is poor
  chk_t c;
  reset(&c, 600);
  burn(&c, CHK_STOVE_PASS_OPEN, 200, 24, 5);
  burn(&c, CHK_STOVE_PASS_HOOD, 140, 24, 5);

  TEST_ASSERT_EQUAL_INT(CHK_STOVE_SOLID, chk_stove_evaluate(&c));
  TEST_ASSERT_FLOAT_WITHIN(0.02, 0.30, c.result[CHK_STOVE_CAPTURE]);
  TEST_ASSERT_EQUAL_INT(CHK_STOVE_TIER_LOW, chk_stove_tier(c.result[CHK_STOVE_CAPTURE]));
}

static void test_a_hood_that_makes_it_worse_catches_nothing() {
  // never report a negative share: the honest answer is that it caught none
  chk_t c;
  reset(&c, 600);
  burn(&c, CHK_STOVE_PASS_OPEN, 100, 24, 5);
  burn(&c, CHK_STOVE_PASS_HOOD, 150, 24, 5);

  TEST_ASSERT_EQUAL_INT(CHK_STOVE_SOLID, chk_stove_evaluate(&c));
  TEST_ASSERT_EQUAL_FLOAT(0, c.result[CHK_STOVE_CAPTURE]);
}

static void test_only_the_opening_window_shapes_the_slope() {
  // readings past two minutes are watched but do not enter the fit, because
  // the built-up excess starts feeding the hood's own removal
  chk_t c;
  reset(&c, 600);

  TEST_ASSERT_TRUE(chk_stove_observe_burn(&c, CHK_STOVE_PASS_OPEN, 700, 0));
  TEST_ASSERT_TRUE(chk_stove_observe_burn(&c, CHK_STOVE_PASS_OPEN, 900, CHK_STOVE_SLOPE_MS));
  TEST_ASSERT_FALSE(chk_stove_observe_burn(&c, CHK_STOVE_PASS_OPEN, 1200, CHK_STOVE_SLOPE_MS + 5000));

  TEST_ASSERT_EQUAL_INT(2, c.accum[CHK_STOVE_PASS_OPEN].n);

  // but the peak is still the highest the room actually reached
  TEST_ASSERT_EQUAL_FLOAT(1200, c.result[CHK_STOVE_PEAK]);
}

static void test_a_burner_that_never_lit_is_unclear() {
  // without a rise there is nothing for a hood to have caught
  chk_t c;
  reset(&c, 600);
  burn(&c, CHK_STOVE_PASS_OPEN, 0, 24, 5);
  burn(&c, CHK_STOVE_PASS_HOOD, 0, 24, 5);

  TEST_ASSERT_EQUAL_INT(CHK_STOVE_UNCLEAR, chk_stove_evaluate(&c));
  TEST_ASSERT_TRUE(c.result[CHK_STOVE_CAPTURE] < 0);
}

static void test_a_missing_pass_is_unclear() {
  chk_t c;
  reset(&c, 600);
  burn(&c, CHK_STOVE_PASS_OPEN, 200, 24, 5);
  // the user walked away before the third pass

  TEST_ASSERT_EQUAL_INT(CHK_STOVE_UNCLEAR, chk_stove_evaluate(&c));
}

static void test_the_clearing_pass_gives_the_hood_rate() {
  chk_t c;
  reset(&c, 600);
  burn(&c, CHK_STOVE_PASS_OPEN, 200, 24, 5);
  burn(&c, CHK_STOVE_PASS_HOOD, 60, 24, 5);
  clear(&c, 1400, 4.1f, 40, 5);

  TEST_ASSERT_EQUAL_INT(CHK_STOVE_SOLID, chk_stove_evaluate(&c));
  TEST_ASSERT_FLOAT_WITHIN(0.2, 4.1, c.result[CHK_STOVE_HOOD_ACH]);
}

static void test_a_capture_result_survives_an_unmeasured_clearing() {
  // the clearing is reported separately, so losing it must not lose the ratio
  chk_t c;
  reset(&c, 600);
  burn(&c, CHK_STOVE_PASS_OPEN, 200, 24, 5);
  burn(&c, CHK_STOVE_PASS_HOOD, 60, 24, 5);

  TEST_ASSERT_EQUAL_INT(CHK_STOVE_SOLID, chk_stove_evaluate(&c));
  TEST_ASSERT_TRUE(c.result[CHK_STOVE_HOOD_ACH] < 0);
  TEST_ASSERT_FLOAT_WITHIN(0.02, 0.70, c.result[CHK_STOVE_CAPTURE]);
}

static void test_the_benchmarks_are_the_scale() {
  // LBNL 2020: 70 per cent for NO2, 60 for PM2.5, installed hoods about 55
  TEST_ASSERT_EQUAL_INT(CHK_STOVE_TIER_HIGH, chk_stove_tier(0.70f));
  TEST_ASSERT_EQUAL_INT(CHK_STOVE_TIER_MID, chk_stove_tier(0.55f));
  TEST_ASSERT_EQUAL_INT(CHK_STOVE_TIER_LOW, chk_stove_tier(0.54f));
}

void suite_chk_stove() {
  RUN_TEST(test_a_good_hood_catches_most_of_it);
  RUN_TEST(test_a_poor_hood_is_the_low_tier);
  RUN_TEST(test_a_hood_that_makes_it_worse_catches_nothing);
  RUN_TEST(test_only_the_opening_window_shapes_the_slope);
  RUN_TEST(test_a_burner_that_never_lit_is_unclear);
  RUN_TEST(test_a_missing_pass_is_unclear);
  RUN_TEST(test_the_clearing_pass_gives_the_hood_rate);
  RUN_TEST(test_a_capture_result_survives_an_unmeasured_clearing);
  RUN_TEST(test_the_benchmarks_are_the_scale);
}
