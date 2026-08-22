#include <unity.h>

#include "pwr.h"
#include "pwr_model.inc"

// the units the model estimates, for the drift checks below
#include "scr.h"
#include "sensor_hal.h"

// the sensor suite drives the real policy layer against a scripted HAL
int32_t test_sensor_apply_gas(int32_t sleep, int32_t window);

// The model hand-copies the clamps and regime thresholds of the units it
// estimates, so pin them here: a change to either unit fails the test build
// until pwr_model.inc is regenerated. The relations the model also mirrors
// (the window is at most half the interval, and only applies in the manual
// regime) cannot be expressed as constants and are checked by
// test_pwr_gas_window_matches_driver() below.
_Static_assert(PWR_SLEEP_MIN_S == AL_SENSOR_MIN_INTERVAL, "power model drift: sleep-rate minimum");
_Static_assert(PWR_SLEEP_MAX_S == AL_SENSOR_MAX_INTERVAL, "power model drift: sleep-rate maximum");
_Static_assert(PWR_LOW_POWER_S == AL_SENSOR_LOW_POWER_INTERVAL, "power model drift: low power threshold");
_Static_assert(PWR_SINGLE_SHOT_S == AL_SENSOR_MANUAL_INTERVAL, "power model drift: single shot threshold");
_Static_assert(PWR_GAS_WINDOW_MIN_S == AL_SENSOR_GAS_MIN_WINDOW, "power model drift: gas window minimum");
_Static_assert(PWR_CYCLED_S * 1000 - AL_SENSOR_MSR_TIME == AL_SENSOR_CYCLE_TIME, "power model drift: cycled threshold");
_Static_assert(PWR_DISPLAY_MIN_S == SCR_DISPLAY_MIN, "power model drift: display-rate minimum");
_Static_assert(PWR_DISPLAY_MAX_S == SCR_DISPLAY_MAX, "power model drift: display-rate maximum");

// The canonical per-rung runtimes of the exported power model. A drifted or
// hand-edited pwr_model.inc, or a formula diverging from the other
// surfaces, fails here.
static const float pwr_expected_days[] = {3.3f, 8.6f, 10.0f, 14.0f, 16.7f, 25.9f, 33.9f, 40.2f, 47.5f, 56.7f, 71.3f};

static void test_pwr_rung_days() {
  TEST_ASSERT_EQUAL_INT(11, pwr_num_rungs());
  for (int i = 0; i < pwr_num_rungs(); i++) {
    pwr_rung_t rung = pwr_rung(i);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, pwr_expected_days[i], (float)pwr_days(rung.sleep, rung.display, rung.gas));
  }
}

static void test_pwr_ladder_monotone() {
  // every rung gains runtime and never measures or refreshes more often
  for (int i = 1; i < pwr_num_rungs(); i++) {
    pwr_rung_t prev = pwr_rung(i - 1);
    pwr_rung_t next = pwr_rung(i);
    TEST_ASSERT(pwr_days(next.sleep, next.display, next.gas) > pwr_days(prev.sleep, prev.display, prev.gas));
    TEST_ASSERT(next.sleep >= prev.sleep);
    TEST_ASSERT(next.display >= prev.display);
  }
}

static void test_pwr_gas_window_clamps() {
  // below the single-shot regime a window is ignored and the heater runs
  // continuously
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)pwr_current(30, 60, 0), (float)pwr_current(30, 60, 120));

  // a window is clamped to at most half the interval and at least 10s
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)pwr_current(600, 300, 300), (float)pwr_current(600, 300, 500));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)pwr_current(600, 300, 10), (float)pwr_current(600, 300, 3));
}

static void test_pwr_defaults() {
  // the shipped configuration (rung index 1)
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.280f, (float)pwr_current(30, 60, 0));
}

// Pins pwr_gas_window() to the driver it mirrors, beyond the thresholds the
// asserts above already cover.
static void check_duty(int32_t sleep, int32_t window) {
  // the model reports the applied window in seconds, the driver its duty in
  // milliseconds, with a disabled SGP shared as -1
  int32_t applied = pwr_gas_window(window, sleep);
  TEST_ASSERT_EQUAL_INT(applied < 0 ? -1 : applied * 1000, test_sensor_apply_gas(sleep, window));
}

static void test_pwr_gas_window_matches_driver() {
  // across the regimes and around both ends of the clamp
  static const int32_t sleeps[] = {5, 29, 30, 59, 60, 61, 120, 179, 180, 600};
  static const int32_t windows[] = {-1, 0, 3, 10, 25, 29, 30, 31, 70, 120, 180, 300, 500};
  for (size_t i = 0; i < sizeof(sleeps) / sizeof(int32_t); i++) {
    for (size_t j = 0; j < sizeof(windows) / sizeof(int32_t); j++) {
      check_duty(sleeps[i], windows[j]);
    }
  }

  // and for every rung the ladder actually ships
  for (int i = 0; i < pwr_num_rungs(); i++) {
    pwr_rung_t rung = pwr_rung(i);
    check_duty(rung.sleep, rung.gas);
  }
}

void suite_pwr() {
  RUN_TEST(test_pwr_rung_days);
  RUN_TEST(test_pwr_ladder_monotone);
  RUN_TEST(test_pwr_gas_window_clamps);
  RUN_TEST(test_pwr_defaults);
  RUN_TEST(test_pwr_gas_window_matches_driver);
}
