#include <math.h>
#include <string.h>

#include <unity.h>

#include "chk.h"
#include "chk_bedroom.h"

// Feeds a synthetic night into the evaluator: the room fills from the
// baseline towards the level it settles at, first order with the time
// constant a bedroom's own leakage gives it, sampled every step_s seconds
// with the temperature and humidity that ride in the same sample.
static void feed(chk_t* c, float c0, float settles_at, float tau_h, int minutes, int step_s) {
  memset(c, 0, sizeof(*c));
  chk_bedroom_reset(c);
  c->result[CHK_BEDROOM_C0] = c0;
  for (int i = 0; i * step_s <= minutes * 60; i++) {
    int32_t t = i * step_s * 1000;
    double hours = t / 3600000.0;
    float co2 = (float)(settles_at - (settles_at - c0) * exp(-hours / tau_h));
    // a night cools a little and dampens a little, which the bands catch
    float tmp = (float)(19.6 - 0.18 * hours);
    float rh = (float)(41.5 + 1.1 * hours);
    chk_bedroom_observe(c, co2, tmp, rh, t);
  }
}

static void test_a_night_is_folded_into_its_statistics() {
  // ten hours from 620 ppm towards 1800, which a closed bedroom with two
  // sleepers reaches in the first few hours
  chk_t c;
  feed(&c, 620, 1800, 2.0f, 600, 60);
  chk_bedroom_evaluate(&c, 600 * 60 * 1000);

  // the peak is where it settled, and the night ended there
  TEST_ASSERT_FLOAT_WITHIN(2, 1792, c.result[CHK_BEDROOM_CMAX]);
  TEST_ASSERT_FLOAT_WITHIN(2, 1792, c.result[CHK_BEDROOM_C1]);
  TEST_ASSERT_EQUAL_FLOAT(620, c.result[CHK_BEDROOM_C0]);

  // the mean is the whole night, not the morning it ended on
  TEST_ASSERT_TRUE(c.result[CHK_BEDROOM_CMEAN] > 1500 && c.result[CHK_BEDROOM_CMEAN] < 1700);

  // it crosses 1150 after 1.19 h and stays there, and never reaches 2600
  TEST_ASSERT_FLOAT_WITHIN(0.1, 8.8, c.result[CHK_BEDROOM_OVER_LOW]);
  TEST_ASSERT_EQUAL_FLOAT(0, c.result[CHK_BEDROOM_OVER_HIGH]);

  // the bands are the night's own, not a reading of zero
  TEST_ASSERT_FLOAT_WITHIN(0.05, 17.8, c.result[CHK_BEDROOM_TMIN]);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 19.6, c.result[CHK_BEDROOM_TMAX]);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 41.5, c.result[CHK_BEDROOM_RHMIN]);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 52.5, c.result[CHK_BEDROOM_RHMAX]);

  // the room had settled by morning, so the flow is an estimate of the
  // balance rather than a floor under it: 3500 / (1790 - 400)
  TEST_ASSERT_EQUAL_FLOAT(1, c.result[CHK_BEDROOM_PLATEAU]);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 2.52, c.result[CHK_BEDROOM_FLOW]);
}

static void test_the_air_each_sleeper_gets() {
  // 0.0035 l/s of CO2 over the excess in parts per million, which is the
  // same litres a second whatever the number of sleepers: each brings their
  // own generation with them
  TEST_ASSERT_FLOAT_WITHIN(0.01, 4.67, chk_bedroom_flow(1150));
  TEST_ASSERT_FLOAT_WITHIN(0.01, 3.5, chk_bedroom_flow(1400));
  TEST_ASSERT_FLOAT_WITHIN(0.01, 1.59, chk_bedroom_flow(2600));

  // at or below the sensor's own reference there is no balance to read
  TEST_ASSERT_EQUAL_FLOAT(0, chk_bedroom_flow(400));
  TEST_ASSERT_EQUAL_FLOAT(0, chk_bedroom_flow(380));
}

static void test_a_night_too_short_reports_no_flow() {
  // ninety minutes is not a night: the room is still filling, and a number
  // taken from it would read as the air the sleepers were getting
  chk_t c;
  feed(&c, 620, 1800, 2.0f, 90, 60);
  chk_bedroom_evaluate(&c, 90 * 60 * 1000);

  TEST_ASSERT_EQUAL_FLOAT(0, c.result[CHK_BEDROOM_FLOW]);

  // the rest of the night still stands
  TEST_ASSERT_TRUE(c.result[CHK_BEDROOM_CMAX] > 1100);
  TEST_ASSERT_TRUE(c.result[CHK_BEDROOM_CMEAN] > 620);
}

static void test_a_room_still_filling_is_not_a_plateau() {
  // a room whose CO2 is still climbing by 300 ppm an hour at the end has not
  // settled, so the flow it implies is a lower bound and the page says so
  chk_t c;
  memset(&c, 0, sizeof(c));
  chk_bedroom_reset(&c);
  c.result[CHK_BEDROOM_C0] = 600;
  for (int i = 0; i <= 180; i++) {
    int32_t t = i * 60 * 1000;
    chk_bedroom_observe(&c, 600 + t / 12000.0f, 19, 45, t);
  }
  chk_bedroom_evaluate(&c, 180 * 60 * 1000);

  TEST_ASSERT_EQUAL_FLOAT(0, c.result[CHK_BEDROOM_PLATEAU]);

  // and the flow is still reported, since the night was long enough to have
  // one: 900 ppm after three hours
  TEST_ASSERT_TRUE(c.result[CHK_BEDROOM_FLOW] > 0);
}

static void test_a_gap_the_check_slept_through_counts_for_nothing() {
  // the hours above a level are the time the check watched, not the span
  // between its first and last reading
  chk_t c;
  memset(&c, 0, sizeof(c));
  chk_bedroom_reset(&c);
  chk_bedroom_observe(&c, 1500, 19, 45, 0);
  chk_bedroom_observe(&c, 1500, 19, 45, 60 * 1000);           // a minute, counted
  chk_bedroom_observe(&c, 1500, 19, 45, 40 * 60 * 1000);      // half an hour of nothing
  chk_bedroom_observe(&c, 1500, 19, 45, 41 * 60 * 1000);      // and a minute more

  TEST_ASSERT_FLOAT_WITHIN(0.001, 2.0 / 60.0, c.result[CHK_BEDROOM_OVER_LOW]);
}

static void test_a_reading_the_sensor_could_not_give_leaves_the_band_alone() {
  chk_t c;
  memset(&c, 0, sizeof(c));
  chk_bedroom_reset(&c);

  // nothing measured yet is not a band at zero
  TEST_ASSERT_TRUE(isnan(c.result[CHK_BEDROOM_TMIN]));

  chk_bedroom_observe(&c, 800, NAN, NAN, 0);
  TEST_ASSERT_TRUE(isnan(c.result[CHK_BEDROOM_TMIN]));
  TEST_ASSERT_TRUE(isnan(c.result[CHK_BEDROOM_RHMAX]));

  chk_bedroom_observe(&c, 800, 18.2f, 44, 5000);
  chk_bedroom_observe(&c, 800, NAN, NAN, 10000);
  TEST_ASSERT_EQUAL_FLOAT(18.2f, c.result[CHK_BEDROOM_TMIN]);
  TEST_ASSERT_EQUAL_FLOAT(18.2f, c.result[CHK_BEDROOM_TMAX]);
  TEST_ASSERT_EQUAL_FLOAT(44, c.result[CHK_BEDROOM_RHMIN]);
}

static void test_the_verdict_scale() {
  // the scale is graded on the night's mean, which is what the sleep studies
  // state their bands on, and not on the peak it touched
  TEST_ASSERT_EQUAL_INT(CHK_BEDROOM_TIER_HIGH, chk_bedroom_tier(620));
  TEST_ASSERT_EQUAL_INT(CHK_BEDROOM_TIER_HIGH, chk_bedroom_tier(799));
  TEST_ASSERT_EQUAL_INT(CHK_BEDROOM_TIER_MID, chk_bedroom_tier(CHK_BEDROOM_TIER_GOOD));
  TEST_ASSERT_EQUAL_INT(CHK_BEDROOM_TIER_MID, chk_bedroom_tier(1149));
  TEST_ASSERT_EQUAL_INT(CHK_BEDROOM_TIER_LOW, chk_bedroom_tier(CHK_BEDROOM_TIER_FAIR));
  TEST_ASSERT_EQUAL_INT(CHK_BEDROOM_TIER_LOW, chk_bedroom_tier(1900));

  // the night of the first test above, which averaged over 1500 ppm, is the
  // lowest step whatever its peak was
  chk_t c;
  feed(&c, 620, 1800, 2.0f, 600, 60);
  chk_bedroom_evaluate(&c, 600 * 60 * 1000);
  TEST_ASSERT_EQUAL_INT(CHK_BEDROOM_TIER_LOW, chk_bedroom_tier(c.result[CHK_BEDROOM_CMEAN]));
}

void suite_chk_bedroom() {
  RUN_TEST(test_a_night_is_folded_into_its_statistics);
  RUN_TEST(test_the_air_each_sleeper_gets);
  RUN_TEST(test_a_night_too_short_reports_no_flow);
  RUN_TEST(test_a_room_still_filling_is_not_a_plateau);
  RUN_TEST(test_a_gap_the_check_slept_through_counts_for_nothing);
  RUN_TEST(test_a_reading_the_sensor_could_not_give_leaves_the_band_alone);
  RUN_TEST(test_the_verdict_scale);
}
