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
static chk_run_state_t run(chk_measure_run_t* r, const chk_measure_cfg_t* cfg, int n, int32_t from, chk_step_t verdict) {
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
}
