#include <string.h>
#include <math.h>

#include <unity.h>

// include the unit directly to access its static state, with the sensor HAL
// interposed via function-like renames, so the suite drives the policy layer
// against a scripted HAL while the store, clock and gas index algorithm stay
// real
#define al_sensor_hal_init fake_hal_init
#define al_sensor_hal_config fake_hal_config
#define al_sensor_hal_heater_off fake_hal_heater_off
#define al_sensor_hal_ready fake_hal_ready
#define al_sensor_hal_read fake_hal_read
#include "../lib/sensor.c"
#undef al_sensor_hal_init
#undef al_sensor_hal_config
#undef al_sensor_hal_heater_off
#undef al_sensor_hal_ready
#undef al_sensor_hal_read

/* Fake HAL */

static al_sensor_hal_state_t *fake_hal_wired;
static al_sensor_hal_ops_t fake_hal_ops;
static int fake_hal_init_calls;
static int fake_hal_config_calls;
static int fake_hal_heater_calls;
static al_sensor_hal_mode_t fake_hal_mode;
static int fake_hal_interval;
static int fake_hal_duty;
static bool fake_hal_ready_flag;
static al_sensor_hal_data_t fake_hal_data;
static al_sensor_hal_err_t fake_hal_read_err;

void fake_hal_init(al_sensor_hal_ops_t ops, al_sensor_hal_state_t *state) {
  fake_hal_init_calls++;
  fake_hal_ops = ops;
  fake_hal_wired = state;
}

al_sensor_hal_err_t fake_hal_config(al_sensor_hal_mode_t mode, int interval, int duty) {
  // record call and mimic the real HAL's state update
  fake_hal_config_calls++;
  fake_hal_mode = mode;
  fake_hal_interval = interval;
  fake_hal_duty = duty;
  al_sensor_state.mode = mode;
  al_sensor_state.interval = interval;
  al_sensor_state.duty = duty;
  al_sensor_state.heat = 0;
  al_sensor_state.raw = 0;
  al_sensor_state.shot = 0;
  return AL_SENSOR_HAL_OK;
}

al_sensor_hal_err_t fake_hal_heater_off() {
  fake_hal_heater_calls++;
  return AL_SENSOR_HAL_OK;
}

bool fake_hal_ready() { return fake_hal_ready_flag; }

al_sensor_hal_err_t fake_hal_read(al_sensor_hal_data_t *data) {
  if (fake_hal_read_err != AL_SENSOR_HAL_OK) {
    return fake_hal_read_err;
  }
  *data = fake_hal_data;
  return AL_SENSOR_HAL_OK;
}

/* Fake PM Sensor */

static bool fake_pm_present;
static int fake_pm_init_calls;
static bool fake_pm_init_reset;
static int fake_pm_run_calls;
static int32_t fake_pm_run_period;
static int32_t fake_pm_run_ttl;
static int fake_pm_burst_calls;
static int32_t fake_pm_burst_ttl;
static int fake_pm_flush_calls;
static int16_t fake_pm_value;
static int64_t fake_pm_until;
static int32_t fake_pm_age_value;

void al_sensor_pm_init(bool reset) {
  fake_pm_init_calls++;
  fake_pm_init_reset = reset;
}

bool al_sensor_pm_present() { return fake_pm_present; }

void al_sensor_pm_run(int32_t period, int32_t ttl) {
  fake_pm_run_calls++;
  fake_pm_run_period = period;
  fake_pm_run_ttl = ttl;
}

void al_sensor_pm_burst(int32_t ttl) {
  fake_pm_burst_calls++;
  fake_pm_burst_ttl = ttl;
}

void al_sensor_pm_flush() { fake_pm_flush_calls++; }

int16_t al_sensor_pm_sample(int64_t epoch) { return fake_pm_until != 0 && epoch <= fake_pm_until ? fake_pm_value : -1; }

int32_t al_sensor_pm_age() { return fake_pm_age_value; }

/* Fake Power & ULP */

static al_power_state_t fake_power;

al_power_state_t al_power_get() { return fake_power; }

static al_sensor_hal_data_t fake_ulp_data[4];
static int fake_ulp_count;
static int fake_ulp_loads;

void al_ulp_load_state(al_sensor_hal_state_t *state) {
  (void)state;
  fake_ulp_loads++;
}

int al_ulp_readings() { return fake_ulp_count; }

al_sensor_hal_data_t al_ulp_get_reading(int index) { return fake_ulp_data[index]; }

/* Fake Tasking */

static const char *fake_repeat_names[8];
static int fake_repeat_count;
static int fake_triggers;

void naos_delay(uint32_t ms) { (void)ms; }

void naos_defer(const char *name, uint32_t delay_ms, naos_func_t func) {
  // run deferred work immediately
  (void)name;
  (void)delay_ms;
  func();
}

naos_timer_t naos_repeat(const char *name, uint32_t period_ms, naos_func_t func) {
  (void)period_ms;
  (void)func;
  if (fake_repeat_count < (int)(sizeof(fake_repeat_names) / sizeof(fake_repeat_names[0]))) {
    fake_repeat_names[fake_repeat_count++] = name;
  }
  return NULL;
}

naos_signal_t naos_signal() { return NULL; }

void naos_trigger(naos_signal_t signal, uint16_t bits, bool clear) {
  (void)signal;
  (void)bits;
  (void)clear;
  fake_triggers++;
}

bool naos_await(naos_signal_t signal, uint16_t bits, bool clear, int32_t timeout_ms) {
  (void)signal;
  (void)bits;
  (void)clear;
  (void)timeout_ms;
  return true;
}

/* Helpers */

void test_store_reset();
void test_clock_set_epoch(int64_t epoch);

#define SNS_BASE 1800000000000LL

static void sensor_reset() {
  // reset fakes
  fake_hal_wired = NULL;
  memset(&fake_hal_ops, 0, sizeof(fake_hal_ops));
  fake_hal_init_calls = 0;
  fake_hal_config_calls = 0;
  fake_hal_heater_calls = 0;
  fake_hal_ready_flag = false;
  memset(&fake_hal_data, 0, sizeof(fake_hal_data));
  fake_hal_read_err = AL_SENSOR_HAL_OK;
  fake_pm_present = false;
  fake_pm_init_calls = 0;
  fake_pm_run_calls = 0;
  fake_pm_burst_calls = 0;
  fake_pm_flush_calls = 0;
  fake_pm_value = 0;
  fake_pm_until = 0;
  fake_pm_age_value = 0;
  memset(&fake_power, 0, sizeof(fake_power));
  fake_ulp_count = 0;
  fake_ulp_loads = 0;
  fake_repeat_count = 0;
  fake_triggers = 0;

  // settle the monitor's internal clock reference at the test epoch, with the
  // policy re-application silenced (the unit and store state touched by this
  // call is reset right below)
  al_sensor_seconds = 0;
  test_clock_set_epoch(SNS_BASE);
  al_sensor_monitor();

  // reset unit and store state
  test_store_reset();
  al_store_set_base(SNS_BASE, false);
  al_sensor_hook = NULL;
  memset(&al_sensor_state, 0, sizeof(al_sensor_state));
  al_sensor_state.mode = AL_SENSOR_HAL_SLEEP;  // a zeroed mode would read as normal
  GasIndexAlgorithm_init_with_sampling_interval(&al_sensor_voc_params, GasIndexAlgorithm_ALGORITHM_TYPE_VOC, 1.f);
  GasIndexAlgorithm_init_with_sampling_interval(&al_sensor_nox_params, GasIndexAlgorithm_ALGORITHM_TYPE_NOX, 1.f);
  al_sensor_switch_comp = SNS_BASE;
  al_sensor_long_comp_curr = 0;
  al_sensor_long_comp_last = SNS_BASE;
  al_sensor_chg_comp_curr = 0;
  al_sensor_gas_last = 0;
  al_sensor_gas_window = 0;
  al_sensor_gas_grace = 0;
  al_sensor_unpowered_since = 0;
  al_sensor_pm_rate = 0;
  al_sensor_pm_manual = false;
  al_sensor_last_raw_temp = NAN;
  al_sensor_last_raw_voc = 0;
  al_sensor_last_raw_nox = 0;
}

static uint16_t raw_tmp(float tmp) { return (uint16_t)((tmp + 45.f) / 175.f * 65535.f); }

static uint16_t raw_hum(float hum) { return (uint16_t)(hum / 100.f * 65535.f); }

static al_sample_t ingest(int64_t epoch, uint16_t co2, float tmp, float hum, uint16_t voc, uint16_t nox, int16_t pm) {
  return al_sensor_ingest(
      (al_sensor_hal_data_t){
          .epoch = epoch,
          .co2 = co2,
          .tmp = raw_tmp(tmp),
          .hum = raw_hum(hum),
          .voc = voc,
          .nox = nox,
          .prs = 1013 * 4096,
      },
      pm);
}

// reference gas index instances stepped in lockstep with the unit
static GasIndexAlgorithmParams ref_voc;
static GasIndexAlgorithmParams ref_nox;

static void ref_gas_reset() {
  GasIndexAlgorithm_init_with_sampling_interval(&ref_voc, GasIndexAlgorithm_ALGORITHM_TYPE_VOC, 1.f);
  GasIndexAlgorithm_init_with_sampling_interval(&ref_nox, GasIndexAlgorithm_ALGORITHM_TYPE_NOX, 1.f);
}

static int16_t ref_gas(GasIndexAlgorithmParams *params, uint16_t raw, int steps, bool cycled) {
  int32_t index = 0;
  for (int i = 0; i < steps; i++) {
    GasIndexAlgorithm_process(params, raw, &index);
  }
  if (index != 0) {
    index |= cycled ? AL_SAMPLE_GAS_CYCLED : 0;
    index |= GasIndexAlgorithm_is_learning(params) ? AL_SAMPLE_GAS_LEARNING : 0;
  }
  return (int16_t)index;
}

/* Tests */

static void test_sensor_ingest_conversion() {
  sensor_reset();
  al_sensor_state.mode = AL_SENSOR_HAL_MANUAL;
  al_sensor_long_comp_last = SNS_BASE + 60000;  // keep the long ramp at rest

  // verify raw value conversion with all compensations at rest
  al_sample_t sample = ingest(SNS_BASE + 60000, 900, 22.5f, 50.f, 0, 0, 123);
  TEST_ASSERT_EQUAL_INT32(60000, sample.off);
  TEST_ASSERT_EQUAL_INT16(900, sample.co2);
  TEST_ASSERT_INT16_WITHIN(1, 2250, sample.tmp);
  TEST_ASSERT_INT16_WITHIN(1, 5000, sample.hum);
  TEST_ASSERT_EQUAL_INT16(0, sample.voc);
  TEST_ASSERT_EQUAL_INT16(0, sample.nox);
  TEST_ASSERT_EQUAL_INT16(1013, sample.prs);
  TEST_ASSERT_EQUAL_INT16(123, sample.pm);

  // verify the sample was ingested into the store
  TEST_ASSERT_EQUAL_INT32(60000, al_store_last().off);
  TEST_ASSERT_EQUAL_size_t(1, al_store_count(AL_STORE_SHORT));

  // verify the raw readings are retained
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.5f, al_sensor_raw_temp());
}

static void test_sensor_ingest_switch_comp() {
  sensor_reset();
  al_sensor_state.mode = AL_SENSOR_HAL_NORMAL;

  // right after a mode switch the temperature is lowered by 3°C and the
  // humidity recompensated upwards
  al_sensor_switch_comp = SNS_BASE + 60000;
  al_sample_t sample = ingest(SNS_BASE + 60000, 900, 22.5f, 50.f, 0, 0, -1);
  TEST_ASSERT_INT16_WITHIN(2, 1950, sample.tmp);
  int16_t expected_hum = (int16_t)(al_sensor_comp_rh(50.f, 22.5f, 19.5f) * 100.f);
  TEST_ASSERT_INT16_WITHIN(3, expected_hum, sample.hum);

  // ten minutes after the switch the compensation has decayed away
  sample = ingest(SNS_BASE + 660000, 900, 22.5f, 50.f, 0, 0, -1);
  TEST_ASSERT_INT16_WITHIN(1, 2250, sample.tmp);
}

static void test_sensor_ingest_long_comp() {
  sensor_reset();
  al_sensor_state.mode = AL_SENSOR_HAL_MANUAL;

  // the long compensation ramps at 0.002°C/s towards the manual mode target
  al_sample_t sample = ingest(SNS_BASE + 100000, 900, 22.5f, 50.f, 0, 0, -1);
  TEST_ASSERT_INT16_WITHIN(2, 2270, sample.tmp);  // 100s -> +0.2°C

  // the ramp clamps at the 1.5°C target (delta clamped to 900s)
  sample = ingest(SNS_BASE + 1100000, 900, 22.5f, 50.f, 0, 0, -1);
  TEST_ASSERT_INT16_WITHIN(2, 2400, sample.tmp);
}

static void test_sensor_ingest_chg_comp() {
  sensor_reset();
  al_sensor_state.mode = AL_SENSOR_HAL_NORMAL;
  al_sensor_switch_comp = SNS_BASE - 700000;  // switch comp decayed

  // fast charging ramps towards -0.5°C at 0.002°C/s
  fake_power.phase = AL_POWER_PHASE_FAST;
  al_sample_t sample = ingest(SNS_BASE + 100000, 900, 22.5f, 50.f, 0, 0, -1);
  TEST_ASSERT_INT16_WITHIN(2, 2230, sample.tmp);  // 100s -> -0.2°C
  sample = ingest(SNS_BASE + 300000, 900, 22.5f, 50.f, 0, 0, -1);
  TEST_ASSERT_INT16_WITHIN(2, 2200, sample.tmp);  // clamped at -0.5°C

  // without power the compensation ramps back towards zero
  fake_power.phase = AL_POWER_PHASE_NONE;
  sample = ingest(SNS_BASE + 400000, 900, 22.5f, 50.f, 0, 0, -1);
  TEST_ASSERT_INT16_WITHIN(2, 2220, sample.tmp);  // -0.3°C
}

static void test_sensor_ingest_gas_replay() {
  sensor_reset();
  ref_gas_reset();
  al_sensor_state.mode = AL_SENSOR_HAL_MANUAL;

  // the first reading runs a single step (still in the initial blackout)
  al_sample_t sample = ingest(SNS_BASE + 60000, 900, 22.5f, 50.f, 27000, 15000, -1);
  TEST_ASSERT_EQUAL_INT16(ref_gas(&ref_voc, 27000, 1, false), sample.voc);
  TEST_ASSERT_EQUAL_INT16(ref_gas(&ref_nox, 15000, 1, false), sample.nox);
  TEST_ASSERT_EQUAL_INT16(0, sample.voc);

  // sparse readings are replayed at the nominal interval to cover the gap
  sample = ingest(SNS_BASE + 70000, 900, 22.5f, 50.f, 27000, 15000, -1);
  TEST_ASSERT_EQUAL_INT16(ref_gas(&ref_voc, 27000, 10, false), sample.voc);
  TEST_ASSERT_EQUAL_INT16(ref_gas(&ref_nox, 15000, 10, false), sample.nox);

  // the step count is rounded to the nearest interval
  sample = ingest(SNS_BASE + 71500, 900, 22.5f, 50.f, 27000, 15000, -1);
  TEST_ASSERT_EQUAL_INT16(ref_gas(&ref_voc, 27000, 2, false), sample.voc);
  TEST_ASSERT_EQUAL_INT16(ref_gas(&ref_nox, 15000, 2, false), sample.nox);

  // a huge gap is capped at 1800 steps, past the blackout the indices are
  // reported with the learning flag set
  sample = ingest(SNS_BASE + 3671500, 900, 22.5f, 50.f, 27000, 15000, -1);
  TEST_ASSERT_EQUAL_INT16(ref_gas(&ref_voc, 27000, 1800, false), sample.voc);
  TEST_ASSERT_EQUAL_INT16(ref_gas(&ref_nox, 15000, 1800, false), sample.nox);
  TEST_ASSERT_NOT_EQUAL_INT16(0, sample.voc);
  TEST_ASSERT_BITS_HIGH(AL_SAMPLE_GAS_LEARNING, sample.voc);

  // verify the raw readings are retained
  TEST_ASSERT_EQUAL_UINT16(27000, al_sensor_raw_voc());
  TEST_ASSERT_EQUAL_UINT16(15000, al_sensor_raw_nox());

  // while duty cycling, VOC is flagged as cycled and NOx reported unavailable
  al_sensor_state.duty = 20000;
  sample = ingest(SNS_BASE + 3676500, 900, 22.5f, 50.f, 27000, 15000, -1);
  ref_gas(&ref_nox, 15000, 5, false);  // keep the reference in lockstep
  TEST_ASSERT_EQUAL_INT16(ref_gas(&ref_voc, 27000, 5, true), sample.voc);
  TEST_ASSERT_EQUAL_INT16(0, sample.nox);

  // a reading taken with a disabled SGP leaves the indices zero and does not
  // advance the algorithm
  int64_t gas_last = al_sensor_gas_last;
  sample = ingest(SNS_BASE + 3681500, 900, 22.5f, 50.f, 0, 0, -1);
  TEST_ASSERT_EQUAL_INT16(0, sample.voc);
  TEST_ASSERT_EQUAL_INT16(0, sample.nox);
  TEST_ASSERT_EQUAL_INT64(gas_last, al_sensor_gas_last);
}

static void test_sensor_set_interval() {
  sensor_reset();

  // verify mode selection and clamping
  al_sensor_set_interval(1);
  TEST_ASSERT_EQUAL_INT(1, fake_hal_config_calls);
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_NORMAL, fake_hal_mode);
  TEST_ASSERT_EQUAL_INT(0, fake_hal_interval);

  // an unchanged interval is not re-applied
  al_sensor_set_interval(5);
  TEST_ASSERT_EQUAL_INT(1, fake_hal_config_calls);

  // 30-59s selects low power mode
  al_sensor_set_interval(30);
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_LOW_POWER, fake_hal_mode);
  al_sensor_set_interval(59);
  TEST_ASSERT_EQUAL_INT(2, fake_hal_config_calls);

  // from 60s on single shots are taken at the interval less the measure time
  al_sensor_set_interval(60);
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_MANUAL, fake_hal_mode);
  TEST_ASSERT_EQUAL_INT(55000, fake_hal_interval);
  al_sensor_set_interval(601);
  TEST_ASSERT_EQUAL_INT(595000, fake_hal_interval);

  // a mode change resets the switch compensation reference
  test_clock_set_epoch(SNS_BASE + 50000);
  al_sensor_set_interval(5);
  TEST_ASSERT_EQUAL_INT64(SNS_BASE + 50000, al_sensor_switch_comp);
}

static void test_sensor_gas_window() {
  sensor_reset();

  // the window only applies in manual mode
  al_sensor_set_gas_window(30);
  al_sensor_set_interval(30);
  TEST_ASSERT_EQUAL_INT(0, fake_hal_duty);

  // the window is clamped to the conditioning time at least
  al_sensor_set_gas_window(5);
  al_sensor_set_interval(120);
  TEST_ASSERT_EQUAL_INT(10000, fake_hal_duty);

  // and to half the interval at most
  al_sensor_set_gas_window(100);
  al_sensor_set_interval(121);
  TEST_ASSERT_EQUAL_INT(60000, fake_hal_duty);

  // a fitting window is applied as given
  al_sensor_set_gas_window(30);
  al_sensor_set_interval(120);
  TEST_ASSERT_EQUAL_INT(30000, fake_hal_duty);

  // a negative window disables the SGP in all modes
  al_sensor_set_gas_window(-1);
  al_sensor_set_interval(5);
  TEST_ASSERT_EQUAL_INT(-1, fake_hal_duty);
}

static void test_sensor_gas_reenable() {
  sensor_reset();

  // disable the SGP and poison the gas state
  al_sensor_set_gas_window(-1);
  al_sensor_set_interval(60);
  TEST_ASSERT_EQUAL_INT(-1, fake_hal_duty);
  al_sensor_gas_last = SNS_BASE;

  // re-enabling resets the gas index algorithms for a fresh learning phase
  al_sensor_set_gas_window(0);
  al_sensor_set_interval(60);
  TEST_ASSERT_EQUAL_INT(0, fake_hal_duty);
  TEST_ASSERT_EQUAL_INT64(0, al_sensor_gas_last);
  TEST_ASSERT_TRUE(GasIndexAlgorithm_is_learning(&al_sensor_voc_params));
  TEST_ASSERT_TRUE(GasIndexAlgorithm_is_learning(&al_sensor_nox_params));
}

static void test_sensor_gas_grace() {
  sensor_reset();
  al_sensor_set_gas_window(30);
  al_sensor_set_gas_grace(300);

  // while USB powered the sensor runs continuously
  fake_power.has_usb = true;
  al_sensor_set_interval(60);
  TEST_ASSERT_EQUAL_INT(0, fake_hal_duty);
  TEST_ASSERT_EQUAL_INT64(0, al_sensor_unpowered_since);

  // power loss starts the grace period, the window still suppressed
  fake_power.has_usb = false;
  al_sensor_set_interval(60);
  TEST_ASSERT_EQUAL_INT64(SNS_BASE, al_sensor_unpowered_since);
  TEST_ASSERT_EQUAL_INT(0, fake_hal_duty);

  // after the grace expires the window is applied
  test_clock_set_epoch(SNS_BASE + 301000);
  al_sensor_set_interval(60);
  TEST_ASSERT_EQUAL_INT(30000, fake_hal_duty);

  // power return resumes continuous operation right away
  fake_power.has_usb = true;
  al_sensor_set_interval(60);
  TEST_ASSERT_EQUAL_INT(0, fake_hal_duty);
  TEST_ASSERT_EQUAL_INT64(0, al_sensor_unpowered_since);
}

static void test_sensor_monitor_base() {
  sensor_reset();

  // a zero store base is claimed by the monitor
  test_store_reset();
  al_sensor_monitor();
  TEST_ASSERT_EQUAL_INT64(SNS_BASE, al_store_get_base());

  // the base is moved along once it is older than five minutes
  for (int i = 1; i <= 7; i++) {
    test_clock_set_epoch(SNS_BASE + i * 45000);
    al_sensor_monitor();
  }
  TEST_ASSERT_EQUAL_INT64(SNS_BASE + 7 * 45000, al_store_get_base());
}

static void test_sensor_monitor_clock_shift() {
  sensor_reset();
  al_sensor_gas_last = SNS_BASE;
  al_sensor_unpowered_since = SNS_BASE;

  // a clock jump shifts the store base and compensation references along,
  // minus the monitor interval
  test_clock_set_epoch(SNS_BASE + 240000);
  al_sensor_monitor();
  TEST_ASSERT_EQUAL_INT64(SNS_BASE + 241000, al_store_get_base());
  TEST_ASSERT_EQUAL_INT64(SNS_BASE + 241000, al_sensor_switch_comp);
  TEST_ASSERT_EQUAL_INT64(SNS_BASE + 241000, al_sensor_long_comp_last);
  TEST_ASSERT_EQUAL_INT64(SNS_BASE + 241000, al_sensor_gas_last);
  TEST_ASSERT_EQUAL_INT64(SNS_BASE + 241000, al_sensor_unpowered_since);

  // a backwards jump shifts them back
  test_clock_set_epoch(SNS_BASE);
  al_sensor_monitor();
  TEST_ASSERT_EQUAL_INT64(SNS_BASE + 2000, al_store_get_base());
  TEST_ASSERT_EQUAL_INT64(SNS_BASE + 2000, al_sensor_switch_comp);
}

static void test_sensor_pm_policy() {
  sensor_reset();

  // without a detected chip the policy is never applied
  al_sensor_set_pm_rate(60, false);
  TEST_ASSERT_EQUAL_INT(0, fake_pm_run_calls);
  TEST_ASSERT_EQUAL_INT32(INT32_MAX, al_sensor_pm_due());

  // with a chip the sensor is duty cycled at the clamped rate
  fake_pm_present = true;
  al_sensor_set_pm_rate(60, false);
  TEST_ASSERT_EQUAL_INT32(60, fake_pm_run_period);
  TEST_ASSERT_EQUAL_INT32(120, fake_pm_run_ttl);
  al_sensor_set_pm_rate(10, false);
  TEST_ASSERT_EQUAL_INT32(30, fake_pm_run_period);
  al_sensor_set_pm_rate(700, false);
  TEST_ASSERT_EQUAL_INT32(600, fake_pm_run_period);

  // a zero rate or manual mode idles the sensor
  al_sensor_set_pm_rate(0, false);
  TEST_ASSERT_EQUAL_INT32(0, fake_pm_run_period);
  al_sensor_set_pm_rate(60, true);
  TEST_ASSERT_EQUAL_INT32(0, fake_pm_run_period);

  // in manual mode measurements are taken by the caller
  al_sensor_pm_measure();
  TEST_ASSERT_EQUAL_INT(1, fake_pm_burst_calls);
  TEST_ASSERT_EQUAL_INT32(120, fake_pm_burst_ttl);
  TEST_ASSERT_EQUAL_INT(1, fake_pm_flush_calls);

  // and are due once the cached reading is older than the rate
  fake_pm_age_value = 10;
  TEST_ASSERT_EQUAL_INT32(50, al_sensor_pm_due());
  fake_pm_age_value = 70;
  TEST_ASSERT_EQUAL_INT32(0, al_sensor_pm_due());

  // bursts are discarded while the sensor measures on its own
  al_sensor_set_pm_rate(60, false);
  al_sensor_pm_measure();
  TEST_ASSERT_EQUAL_INT(1, fake_pm_burst_calls);
}

static al_sample_t hook_last;
static int hook_calls;

static void test_sensor_hook(al_sample_t sample) {
  hook_last = sample;
  hook_calls++;
}

static void test_sensor_check_read() {
  sensor_reset();
  hook_calls = 0;
  al_sensor_state.mode = AL_SENSOR_HAL_MANUAL;
  al_sensor_config(test_sensor_hook);

  // load HAL data and a cached PM reading
  fake_hal_data = (al_sensor_hal_data_t){
      .epoch = SNS_BASE + 60000,
      .co2 = 900,
      .tmp = raw_tmp(22.5f),
      .hum = raw_hum(50.f),
      .prs = 1013 * 4096,
  };
  fake_pm_value = 55;
  fake_pm_until = SNS_BASE + 60000;

  // without a ready measurement nothing happens
  al_sensor_check();
  TEST_ASSERT_EQUAL_size_t(0, al_store_count(AL_STORE_SHORT));

  // a ready measurement is read, ingested and dispatched
  fake_hal_ready_flag = true;
  al_sensor_check();
  TEST_ASSERT_EQUAL_size_t(1, al_store_count(AL_STORE_SHORT));
  TEST_ASSERT_EQUAL_INT(1, fake_triggers);
  TEST_ASSERT_EQUAL_INT(1, hook_calls);
  TEST_ASSERT_EQUAL_INT16(900, hook_last.co2);
  TEST_ASSERT_EQUAL_INT16(55, hook_last.pm);

  // the last sample is served to awaiting consumers
  TEST_ASSERT_EQUAL_INT16(900, al_sensor_next().co2);

  // a failing HAL read is dropped
  fake_hal_read_err = AL_SENSOR_HAL_ERR_TRANSFER | AL_SENSOR_HAL_ERR_SCD41;
  al_sensor_check();
  TEST_ASSERT_EQUAL_size_t(1, al_store_count(AL_STORE_SHORT));
  TEST_ASSERT_EQUAL_INT(1, hook_calls);
}

static void test_sensor_sleep_off() {
  sensor_reset();

  // sleeping while duty cycling in manual mode turns the heater off
  al_sensor_state.mode = AL_SENSOR_HAL_MANUAL;
  al_sensor_state.duty = 20000;
  al_sensor_state.heat = 5;
  al_sensor_state.raw = 6;
  al_sensor_sleep();
  TEST_ASSERT_EQUAL_INT(1, fake_hal_heater_calls);
  TEST_ASSERT_EQUAL_INT64(0, al_sensor_state.heat);
  TEST_ASSERT_EQUAL_INT64(0, al_sensor_state.raw);

  // in continuous operation the heater stays on
  al_sensor_state.duty = 0;
  al_sensor_sleep();
  TEST_ASSERT_EQUAL_INT(1, fake_hal_heater_calls);

  // turning the sensor off puts the HAL to sleep
  al_sensor_off();
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_SLEEP, fake_hal_mode);
}

static void test_sensor_init_reset() {
  sensor_reset();

  // a cold init resets the sensor into normal mode and starts the tasks
  al_sensor_init(true);
  TEST_ASSERT_EQUAL_INT(1, fake_pm_init_calls);
  TEST_ASSERT_TRUE(fake_pm_init_reset);
  TEST_ASSERT_EQUAL_INT(1, fake_hal_init_calls);
  TEST_ASSERT_EQUAL_PTR(&al_sensor_state, fake_hal_wired);
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_NORMAL, fake_hal_mode);
  TEST_ASSERT_EQUAL_INT64(SNS_BASE, al_sensor_switch_comp);
  TEST_ASSERT_EQUAL_INT64(0, al_sensor_gas_last);
  TEST_ASSERT_EQUAL_INT(0, fake_ulp_loads);
  TEST_ASSERT_EQUAL_INT(2, fake_repeat_count);
  TEST_ASSERT_EQUAL_STRING("al-sns-c", fake_repeat_names[0]);
  TEST_ASSERT_EQUAL_STRING("al-sns-m", fake_repeat_names[1]);
}

static void test_sensor_init_wake() {
  sensor_reset();
  al_sensor_state.mode = AL_SENSOR_HAL_MANUAL;

  // prepare buffered ULP readings with a cached PM value covering them
  fake_ulp_count = 2;
  fake_ulp_data[0] = (al_sensor_hal_data_t){
      .epoch = SNS_BASE + 5000, .co2 = 800, .tmp = raw_tmp(22.f), .hum = raw_hum(50.f), .prs = 1013 * 4096};
  fake_ulp_data[1] = (al_sensor_hal_data_t){
      .epoch = SNS_BASE + 10000, .co2 = 810, .tmp = raw_tmp(22.f), .hum = raw_hum(50.f), .prs = 1013 * 4096};
  fake_pm_value = 55;
  fake_pm_until = SNS_BASE + 7000;  // covers only the first reading

  // a wake init restores the HAL state and backfills the ULP readings
  al_sensor_init(false);
  TEST_ASSERT_EQUAL_INT(1, fake_ulp_loads);
  TEST_ASSERT_EQUAL_INT(0, fake_hal_config_calls);
  TEST_ASSERT_EQUAL_size_t(2, al_store_count(AL_STORE_SHORT));
  TEST_ASSERT_EQUAL_INT16(800, al_store_get(AL_STORE_SHORT, 0).co2);
  TEST_ASSERT_EQUAL_INT16(55, al_store_get(AL_STORE_SHORT, 0).pm);
  TEST_ASSERT_EQUAL_INT16(810, al_store_last().co2);
  TEST_ASSERT_EQUAL_INT16(-1, al_store_last().pm);
}

/* Suite */

// Applies a sleep interval and gas window through the real policy layer and
// reports the duty it configures, so the power model suite can check its
// mirror of this unit against it.
int32_t test_sensor_apply_gas(int32_t sleep, int32_t window) {
  sensor_reset();
  al_sensor_set_gas_window(window);
  al_sensor_set_interval(sleep);
  return fake_hal_duty;
}

void suite_sensor() {
  RUN_TEST(test_sensor_ingest_conversion);
  RUN_TEST(test_sensor_ingest_switch_comp);
  RUN_TEST(test_sensor_ingest_long_comp);
  RUN_TEST(test_sensor_ingest_chg_comp);
  RUN_TEST(test_sensor_ingest_gas_replay);
  RUN_TEST(test_sensor_set_interval);
  RUN_TEST(test_sensor_gas_window);
  RUN_TEST(test_sensor_gas_reenable);
  RUN_TEST(test_sensor_gas_grace);
  RUN_TEST(test_sensor_monitor_base);
  RUN_TEST(test_sensor_monitor_clock_shift);
  RUN_TEST(test_sensor_pm_policy);
  RUN_TEST(test_sensor_check_read);
  RUN_TEST(test_sensor_sleep_off);
  RUN_TEST(test_sensor_init_reset);
  RUN_TEST(test_sensor_init_wake);
}
