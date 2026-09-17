#include <string.h>

#include <unity.h>

#include "chk.h"

// the ventilation measurement's own shape: at least 90 s, at most 5 min,
// 60 samples, and a prompt after two minutes of nothing happening
static const chk_measure_cfg_t vent = {
    .min_ms = 90000,
    .max_ms = 300000,
    .capacity = 60,
    .nudge_ms = 120000,
};

// a baseline instead: no clock at all, just a count
static const chk_measure_cfg_t baseline = {
    .capacity = 6,
};

// runs n good readings at a 5 s cadence from the given start, all saying the
// same thing, and returns where the engine got to
static chk_run_state_t run(chk_measure_run_t* r, const chk_measure_cfg_t* cfg, int n, int32_t from,
                           chk_step_t verdict) {
  chk_run_state_t state = CHK_RUN_GO;
  for (int i = 0; i < n && state == CHK_RUN_GO; i++) {
    state = chk_measure_step(cfg, r, from + (i + 1) * 5000, true, verdict);
  }
  return state;
}

static void test_a_baseline_stops_at_its_count() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  TEST_ASSERT_EQUAL_INT(CHK_RUN_DONE, run(&r, &baseline, 10, 0, CHK_STEP_WAIT));
  TEST_ASSERT_EQUAL_INT(6, r.count);
}

static void test_a_check_cannot_stop_before_the_minimum() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  // the signal is satisfied immediately, but 30 s is not a measurement
  TEST_ASSERT_EQUAL_INT(CHK_RUN_GO, run(&r, &vent, 6, 0, CHK_STEP_DONE));
  TEST_ASSERT_EQUAL_INT(30000, r.elapsed);

  // once past the minimum the same verdict ends it
  TEST_ASSERT_EQUAL_INT(CHK_RUN_DONE, run(&r, &vent, 20, 30000, CHK_STEP_DONE));
  TEST_ASSERT_TRUE(r.elapsed >= vent.min_ms);
}

static void test_a_run_stops_at_the_maximum_even_while_moving() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  // still falling at five minutes: the check does not get to run forever
  chk_run_state_t state = CHK_RUN_GO;
  int32_t t = 0;
  while (state == CHK_RUN_GO && t < 600000) {
    t += 5000;
    state = chk_measure_step(&vent, &r, t, true, CHK_STEP_GO);
  }
  TEST_ASSERT_EQUAL_INT(CHK_RUN_DONE, state);
  TEST_ASSERT_EQUAL_INT(300000, r.elapsed);
}

static void test_a_run_stops_at_capacity() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  // a slow cadence reaches sixty samples before it reaches five minutes
  chk_run_state_t state = CHK_RUN_GO;
  for (int i = 1; i <= 60 && state == CHK_RUN_GO; i++) {
    state = chk_measure_step(&vent, &r, i * 1000, true, CHK_STEP_GO);
  }
  TEST_ASSERT_EQUAL_INT(CHK_RUN_DONE, state);
  TEST_ASSERT_EQUAL_INT(60, r.count);
}

static void test_a_bad_sensor_ends_the_run() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  chk_run_state_t state = CHK_RUN_GO;
  for (int i = 1; i <= 12 && state == CHK_RUN_GO; i++) {
    state = chk_measure_step(&vent, &r, i * 5000, false, CHK_STEP_WAIT);
  }
  TEST_ASSERT_EQUAL_INT(CHK_RUN_FAILED, state);

  // and not before the failures could mean anything
  TEST_ASSERT_TRUE(r.attempts >= CHK_FAIL_MIN_ATTEMPTS);
}

static void test_an_occasional_dropout_is_tolerated() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  // one reading in five fails, which is under the quarter that ends a run
  chk_run_state_t state = CHK_RUN_GO;
  for (int i = 1; i <= 40 && state == CHK_RUN_GO; i++) {
    state = chk_measure_step(&vent, &r, i * 2000, i % 5 != 0, CHK_STEP_GO);
  }
  TEST_ASSERT_NOT_EQUAL(CHK_RUN_FAILED, state);
  TEST_ASSERT_TRUE(r.fails > 0);
}

static void test_a_failed_reading_does_not_count_as_a_sample() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  chk_measure_step(&baseline, &r, 5000, false, CHK_STEP_WAIT);
  TEST_ASSERT_EQUAL_INT(0, r.count);
  TEST_ASSERT_EQUAL_INT(1, r.fails);
  TEST_ASSERT_EQUAL_INT(1, r.attempts);
}

static void test_the_prompt_appears_and_clears() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  // nothing is happening, but it is too early to nag
  run(&r, &vent, 10, 0, CHK_STEP_WAIT);
  TEST_ASSERT_FALSE(r.nudging);

  // two minutes of nothing: ask whether the window is really open
  chk_measure_step(&vent, &r, 120000, true, CHK_STEP_WAIT);
  TEST_ASSERT_TRUE(r.nudging);

  // the moment the signal moves, stop nagging
  chk_measure_step(&vent, &r, 125000, true, CHK_STEP_GO);
  TEST_ASSERT_FALSE(r.nudging);
}

static void test_a_dropout_does_not_change_the_prompt() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  chk_measure_step(&vent, &r, 120000, true, CHK_STEP_WAIT);
  TEST_ASSERT_TRUE(r.nudging);

  // an unusable reading says nothing about whether the window is open
  chk_measure_step(&vent, &r, 125000, false, CHK_STEP_GO);
  TEST_ASSERT_TRUE(r.nudging);
}

// a settling run: two minutes at least, five at most, no count
static const chk_measure_cfg_t settling = {
    .min_ms = 120000,
    .max_ms = 300000,
    .settle = true,
};

// feeds one reading through the classifier and the engine at a 5 s cadence,
// as chk_catch_up does, and returns the engine's state
static chk_run_state_t settle_step(chk_measure_run_t* r, float value, int32_t t) {
  chk_step_t verdict = chk_measure_classify(&settling, r, value, t, CHK_STEP_WAIT);
  return chk_measure_step(&settling, r, t, true, verdict);
}

// a first-order approach from `from` towards `to` with a one-minute time
// constant, which is the sensor's, plus a little deterministic noise
static float approach(float from, float to, int32_t t, float noise) {
  return (float)(to + (from - to) * exp(-t / 60000.0) + ((t / 5000) % 7 - 3) * noise);
}

static void test_a_step_settles_once_it_holds_for_a_minute() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  // room air down to outdoor air, a 450 ppm step
  chk_run_state_t state = CHK_RUN_GO;
  int32_t t = 0;
  while (state == CHK_RUN_GO && t < 600000) {
    t += 5000;
    state = settle_step(&r, approach(880, 430, t, 1.5f), t);
  }
  TEST_ASSERT_EQUAL_INT(CHK_RUN_DONE, state);
  TEST_ASSERT_TRUE(chk_settle_done(&r));

  // about three minutes: after the floor, well before the cap
  TEST_ASSERT_TRUE(r.elapsed >= settling.min_ms);
  TEST_ASSERT_TRUE(r.elapsed >= 150000 && r.elapsed <= 240000);

  // and the value is the newest window's median, within the residual the
  // band allows of the true floor
  TEST_ASSERT_FLOAT_WITHIN(30, 430, chk_settle_value(&r));
}

static void test_a_flat_reading_settles_at_the_floor() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  // the device was already in the air it is measuring: only noise
  chk_run_state_t state = CHK_RUN_GO;
  int32_t t = 0;
  while (state == CHK_RUN_GO && t < 600000) {
    t += 5000;
    state = settle_step(&r, approach(600, 600, t, 2.0f), t);
  }
  TEST_ASSERT_EQUAL_INT(CHK_RUN_DONE, state);
  TEST_ASSERT_EQUAL_INT(settling.min_ms, r.elapsed);
  TEST_ASSERT_TRUE(chk_settle_done(&r));
}

static void test_a_room_with_a_person_in_it_still_settles() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  // the sensor has caught up but the room climbs 500 ppm an hour, which is a
  // person in a small room: 4 ppm per window, inside the band
  chk_run_state_t state = CHK_RUN_GO;
  int32_t t = 0;
  while (state == CHK_RUN_GO && t < 600000) {
    t += 5000;
    state = settle_step(&r, 800 + t * (500.0f / 3600000), t);
  }
  TEST_ASSERT_EQUAL_INT(CHK_RUN_DONE, state);
  TEST_ASSERT_TRUE(chk_settle_done(&r));
  TEST_ASSERT_EQUAL_INT(settling.min_ms, r.elapsed);
}

static void test_a_reading_still_drifting_is_taken_at_the_cap() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  // a steady 40 ppm a minute, which never holds
  chk_run_state_t state = CHK_RUN_GO;
  int32_t t = 0;
  while (state == CHK_RUN_GO && t < 600000) {
    t += 5000;
    state = settle_step(&r, 1200 - t * (40.0f / 60000), t);
  }
  TEST_ASSERT_EQUAL_INT(CHK_RUN_DONE, state);
  TEST_ASSERT_EQUAL_INT(settling.max_ms, r.elapsed);

  // taken as it stands, and marked as such
  TEST_ASSERT_FALSE(chk_settle_done(&r));
  TEST_ASSERT_FALSE(isnan(chk_settle_value(&r)));
}

static void test_one_wild_reading_neither_breaks_nor_fakes_settling() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  // flat air with one reading a hundred ppm off in each window
  chk_run_state_t state = CHK_RUN_GO;
  int32_t t = 0;
  while (state == CHK_RUN_GO && t < 600000) {
    t += 5000;
    float v = (t / 5000) % 6 == 3 ? 700 : 600;
    state = settle_step(&r, v, t);
  }
  TEST_ASSERT_EQUAL_INT(settling.min_ms, r.elapsed);
  TEST_ASSERT_FLOAT_WITHIN(0.5, 600, chk_settle_value(&r));
}

static void test_the_estimate_counts_down_and_stays_inside_the_frame() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  // before two windows there is only the floor to promise
  TEST_ASSERT_EQUAL_INT32(settling.min_ms, chk_measure_remaining(&settling, &r, 0));
  TEST_ASSERT_EQUAL_INT32(settling.min_ms - 30000, chk_measure_remaining(&settling, &r, 30000));

  // a 450 ppm step: at one minute the windows differ by a lot, so the
  // estimate reaches past the floor, and it shrinks as the sensor catches up
  int32_t t = 0;
  int32_t previous = INT32_MAX;
  chk_run_state_t state = CHK_RUN_GO;
  while (state == CHK_RUN_GO && t < 600000) {
    t += 5000;
    state = settle_step(&r, approach(880, 430, t, 0), t);
    int32_t left = chk_measure_remaining(&settling, &r, t);
    TEST_ASSERT_TRUE(left >= 0);
    TEST_ASSERT_TRUE(left >= settling.min_ms - t);
    TEST_ASSERT_TRUE(left <= settling.max_ms - t);
    if (t == 60000) {
      TEST_ASSERT_TRUE_MESSAGE(left > settling.min_ms - t, "a big step promises more than the floor");
      TEST_ASSERT_TRUE(t + left >= 150000 && t + left <= 240000);
    }
    // the promise re-estimates each window and may only slip by one, since
    // settling is only ever noticed at a window's close
    if (t % 30000 == 0 && t > 60000) {
      TEST_ASSERT_TRUE_MESSAGE(t + left <= previous + 30000, "the promised end slips by at most a window");
    }
    if (t % 30000 == 0) {
      previous = t + left;
    }
  }
  TEST_ASSERT_EQUAL_INT(CHK_RUN_DONE, state);
  TEST_ASSERT_EQUAL_INT32(0, chk_measure_remaining(&settling, &r, r.elapsed));

  // a counted run has no estimate to give
  TEST_ASSERT_EQUAL_INT32(-1, chk_measure_remaining(&baseline, &r, 0));
}

static void test_a_baseline_never_prompts() {
  chk_measure_run_t r;
  chk_measure_reset(&r);

  // no nudge_ms: a baseline has nothing to ask the user for
  chk_measure_step(&baseline, &r, 600000, true, CHK_STEP_WAIT);
  TEST_ASSERT_FALSE(r.nudging);
}

void suite_chk_measure() {
  RUN_TEST(test_a_baseline_stops_at_its_count);
  RUN_TEST(test_a_check_cannot_stop_before_the_minimum);
  RUN_TEST(test_a_run_stops_at_the_maximum_even_while_moving);
  RUN_TEST(test_a_run_stops_at_capacity);
  RUN_TEST(test_a_bad_sensor_ends_the_run);
  RUN_TEST(test_an_occasional_dropout_is_tolerated);
  RUN_TEST(test_a_failed_reading_does_not_count_as_a_sample);
  RUN_TEST(test_the_prompt_appears_and_clears);
  RUN_TEST(test_a_dropout_does_not_change_the_prompt);
  RUN_TEST(test_a_baseline_never_prompts);
  RUN_TEST(test_a_step_settles_once_it_holds_for_a_minute);
  RUN_TEST(test_a_flat_reading_settles_at_the_floor);
  RUN_TEST(test_a_room_with_a_person_in_it_still_settles);
  RUN_TEST(test_a_reading_still_drifting_is_taken_at_the_cap);
  RUN_TEST(test_one_wild_reading_neither_breaks_nor_fakes_settling);
  RUN_TEST(test_the_estimate_counts_down_and_stays_inside_the_frame);
}
