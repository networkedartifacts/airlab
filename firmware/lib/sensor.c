#include <math.h>
#include <naos.h>
#include <naos/sys.h>

#include <al/core.h>
#include <al/sensor.h>
#include <al/clock.h>
#include <al/store.h>
#include <al/power.h>

#include "internal.h"
#include "sensor_hal.h"
#include "sensor_gas.h"

#define AL_SENSOR_DEBUG false

static naos_mutex_t al_sensor_mutex;
static naos_signal_t al_sensor_signal;
static al_sensor_hook_t al_sensor_hook;

AL_KEEP static al_sensor_hal_state_t al_sensor_state = {0};
AL_KEEP static GasIndexAlgorithmParams al_sensor_voc_params = {0};
AL_KEEP static GasIndexAlgorithmParams al_sensor_nox_params = {0};
AL_KEEP static int64_t al_sensor_switch_comp = 0;
AL_KEEP static float al_sensor_long_comp_curr = 0;
AL_KEEP static int64_t al_sensor_long_comp_last = 0;
AL_KEEP static float al_sensor_chg_comp_curr = 0;
AL_KEEP static int64_t al_sensor_gas_last = 0;
static float al_sensor_last_raw_temp = NAN;
static uint16_t al_sensor_last_raw_voc = 0;
static uint16_t al_sensor_last_raw_nox = 0;

#define AL_SENSOR_CHG_COMP_RATE 0.002f  // ramp rate (°C/s)

#define AL_SENSOR_GAS_STEP 5000  // gas index sampling interval (ms)

static const float al_sensor_chg_comp_target[] = {
    [AL_POWER_PHASE_NONE] = 0.0f,
    [AL_POWER_PHASE_USB] = -0.2f,   // max 1.6min
    [AL_POWER_PHASE_PRE] = -0.3f,   // max 2.5min
    [AL_POWER_PHASE_FAST] = -0.5f,  // max 4.2min
    [AL_POWER_PHASE_TERM] = 0.3f,   // max 2.5min
};

static struct {
  float target;
  float rate;
} al_sensor_long_comp[] = {
    [AL_SENSOR_HAL_NORMAL] = {.target = 0.f, .rate = 0.03f},       // max 50s
    [AL_SENSOR_HAL_LOW_POWER] = {.target = 1.0f, .rate = 0.003f},  // max 5min
    [AL_SENSOR_HAL_MANUAL] = {.target = 1.5f, .rate = 0.002f},     // max 12.5min (tuned at 60s)
};

static float al_sensor_clamp(float v, float lo, float hi) {
  // return clamped value
  return v < lo ? lo : (v > hi ? hi : v);
}

static float al_sensor_ramp(float curr, float target, float rate, float dt) {
  if (curr < target) {
    curr += dt * rate;
    return curr > target ? target : curr;
  } else if (curr > target) {
    curr -= dt * rate;
    return curr < target ? target : curr;
  }
  return curr;
}

static al_sensor_hal_err_t al_sensor_transfer(uint8_t target, uint8_t *wd, size_t wl, uint8_t *rd, size_t rl) {
  // perform transfer
  esp_err_t err = al_i2c_transfer(target, wd, wl, rd, rl, 1000);
  if (err == ESP_ERR_TIMEOUT) {
    return AL_SENSOR_HAL_ERR_TIMEOUT;
  } else if (err != ESP_OK) {
    return AL_SENSOR_HAL_ERR_TRANSFER;
  }

  return AL_SENSOR_HAL_OK;
}

static float al_sensor_comp_rh(float rh, float t_raw, float t_comp) {
  // Tetens formula for saturation vapor pressure
  float es_raw = 6.112f * expf((17.62f * t_raw) / (243.12f + t_raw));
  float es_comp = 6.112f * expf((17.62f * t_comp) / (243.12f + t_comp));
  float ah = rh * es_raw / 100.0f;  // absolute humidity proxy
  return (ah / es_comp) * 100.0f;   // recomputed RH at compensated T
}

static al_sample_t al_sensor_ingest(al_sensor_hal_data_t data) {
  // calculate ppm, °C, % rH
  float co2 = (float)data.co2;
  float tmp = -45.f + 175.f * ((float)data.tmp / (float)(UINT16_MAX));
  float hum = 100.f * ((float)data.hum / (float)(UINT16_MAX));

  // update last raw temperature
  al_sensor_last_raw_temp = tmp;

  // apply mode switch temperature compensation
  if (al_sensor_state.mode != AL_SENSOR_HAL_MANUAL) {
    // we use the formula "tmp − max(3 * exp(−0.015 * seconds), 0)" to compensate
    // the temperature for the first 10 minutes after a mode switch
    float seconds = al_sensor_clamp((float)(data.epoch - al_sensor_switch_comp) / 1000.f, 0, 600);
    float tmp_comp = tmp - 3.f * expf(-0.015f * seconds);
    float hum_comp = al_sensor_comp_rh(hum, tmp, tmp_comp);
    if (AL_SENSOR_DEBUG) {
      naos_log("al-sns: switch comp tmp=%.2f -> %.2f, hum=%.2f -> %.2f (seconds=%.1f)", tmp, tmp_comp, hum, hum_comp,
               seconds);
    }
    tmp = tmp_comp;
    hum = hum_comp;
  }

  // calculate time delta for compensation ramps
  float comp_delta = al_sensor_clamp((float)(data.epoch - al_sensor_long_comp_last) / 1000.f, 0, 900);

  // advance long compensation
  float long_target = al_sensor_long_comp[al_sensor_state.mode].target;
  float long_rate = al_sensor_long_comp[al_sensor_state.mode].rate;
  float long_prev = al_sensor_long_comp_curr;
  al_sensor_long_comp_curr = al_sensor_ramp(al_sensor_long_comp_curr, long_target, long_rate, comp_delta);
  if (AL_SENSOR_DEBUG && al_sensor_long_comp_curr != long_prev) {
    naos_log("al-sns: long comp updated: curr=%.3f°C, target=%.3f°C", al_sensor_long_comp_curr, long_target);
  }
  al_sensor_long_comp_last = data.epoch;

  // apply long compensation
  if (al_sensor_long_comp_curr != 0.f) {
    hum = al_sensor_comp_rh(hum, tmp, tmp + al_sensor_long_comp_curr);
    tmp += al_sensor_long_comp_curr;
    if (AL_SENSOR_DEBUG) {
      naos_log("al-sns: long comp applied: %.3f°C", al_sensor_long_comp_curr);
    }
  }

  // advance charging compensation
  float chg_target = al_sensor_chg_comp_target[al_power_get().phase];
  float chg_prev = al_sensor_chg_comp_curr;
  al_sensor_chg_comp_curr = al_sensor_ramp(al_sensor_chg_comp_curr, chg_target, AL_SENSOR_CHG_COMP_RATE, comp_delta);
  if (AL_SENSOR_DEBUG && al_sensor_chg_comp_curr != chg_prev) {
    naos_log("al-sns: chg comp updated: curr=%.3f°C, target=%.3f°C", al_sensor_chg_comp_curr, chg_target);
  }

  // apply charging compensation
  if (al_sensor_chg_comp_curr != 0.f) {
    hum = al_sensor_comp_rh(hum, tmp, tmp + al_sensor_chg_comp_curr);
    tmp += al_sensor_chg_comp_curr;
    if (AL_SENSOR_DEBUG) {
      naos_log("al-sns: chg comp applied: %.3f°C", al_sensor_chg_comp_curr);
    }
  }

  // update last raw VOC/NOx readings
  al_sensor_last_raw_voc = data.voc;
  al_sensor_last_raw_nox = data.nox;

  // determine gas index steps to cover the elapsed time at the nominal
  // sampling interval, as replaying sparse samples keeps the algorithm's
  // sample-count based time constants wall-clock correct
  int32_t gas_steps = 1;
  if (al_sensor_gas_last != 0) {
    gas_steps = (int32_t)((data.epoch - al_sensor_gas_last + AL_SENSOR_GAS_STEP / 2) / AL_SENSOR_GAS_STEP);
    gas_steps = gas_steps < 1 ? 1 : (gas_steps > 180 ? 180 : gas_steps);
  }
  al_sensor_gas_last = data.epoch;

  // perform gas index calculation
  int32_t voc_index = 0;
  int32_t nox_index = 0;
  for (int32_t i = 0; i < gas_steps; i++) {
    GasIndexAlgorithm_process(&al_sensor_voc_params, data.voc, &voc_index);
    GasIndexAlgorithm_process(&al_sensor_nox_params, data.nox, &nox_index);
  }

  // calculate pressure
  float prs = (float)data.prs / 4096.f;

  // create sample
  al_sample_t sample = {
      .off = (int32_t)(data.epoch - al_store_get_base()),
      .co2 = (int16_t)co2,
      .tmp = (int16_t)(tmp * 100.f),
      .hum = (int16_t)(hum * 100.f),
      .voc = (int16_t)voc_index,
      .nox = (int16_t)nox_index,
      .prs = (int16_t)prs,
  };
  if (AL_SENSOR_DEBUG) {
    int64_t diff = sample.off - al_store_last().off;
    naos_log("al-sns: ingest co2=%d tmp=%d hum=%d voc=%d nox=%d prs=%d ingest off=%d epoch=%lld diff=%lld", sample.co2,
             sample.tmp, sample.hum, sample.voc, sample.nox, sample.prs, sample.off, data.epoch, diff);
  }

  // ingest sample
  al_store_ingest(sample);

  return sample;
}

static void al_sensor_read() {
  // acquire mutex
  naos_lock(al_sensor_mutex);

  // read sensor
  al_sensor_hal_data_t data;
  esp_err_t err = al_sensor_hal_read(&data);
  if (err != AL_SENSOR_HAL_OK) {
    naos_log("al-sns: HAL read error=%d", err);
    naos_unlock(al_sensor_mutex);
    return;
  }

  // ingest data
  al_sample_t sample = al_sensor_ingest(data);

  // release mutex
  naos_unlock(al_sensor_mutex);

  // trigger signal
  naos_trigger(al_sensor_signal, 1, false);

  // dispatch event
  if (al_sensor_hook != NULL) {
    al_sensor_hook(sample);
  }
}

static void al_sensor_check() {
  // check if measurement is available
  naos_lock(al_sensor_mutex);
  bool ready = al_sensor_hal_ready();
  naos_unlock(al_sensor_mutex);

  // defer read if ready
  if (ready) {
    naos_defer("al-sns-r", 0, al_sensor_read);
  }
}

static void al_sensor_monitor() {
  // get time
  int64_t now = al_clock_get_epoch();

  /* check if store needs to be shifted */

  // move store base if zero, or older than 5 minutes
  int64_t store_base = al_store_get_base();
  if (store_base == 0 || now - store_base >= 5 * 60 * 1000) {
    al_store_set_base(now, true);
  }

  /* check if clock has been changed */

  // prepare last epoch
  static int64_t last_epoch = 0;
  if (last_epoch == 0) {
    last_epoch = now;
  }

  // get difference
  int64_t diff = now - last_epoch;

  // update epoch
  last_epoch = now;

  // stop if less than 1 minute
  if (diff < 60 * 1000 && diff > -60 * 1000) {
    return;
  }

  // if the difference is larger than 1 minute we assume that the clock has been
  // changed. to remediate this we will just shift the store base by the changed
  // amount and leave all samples in place

  // remove interval from difference
  diff += 1000;
  naos_log("al-sns: clock shift detected: shifting store base by %lld ms", diff);

  // set new store base
  naos_lock(al_sensor_mutex);
  al_store_set_base(al_store_get_base() + diff, false);
  naos_unlock(al_sensor_mutex);

  // adjust compensation times
  al_sensor_switch_comp += diff;
  al_sensor_long_comp_last += diff;
  if (al_sensor_gas_last != 0) {
    al_sensor_gas_last += diff;
  }
}

void al_sensor_init(bool reset) {
  // create mutex and signal
  al_sensor_mutex = naos_mutex();
  al_sensor_signal = naos_signal();

  // load ULP sensor state if not reset
  if (!reset) {
    al_ulp_load_state(&al_sensor_state);
  }

  // wire sensor HAL
  al_sensor_hal_init(
      (al_sensor_hal_ops_t){
          .transfer = al_sensor_transfer,
          .delay = naos_delay,
          .epoch = al_clock_get_epoch,
      },
      &al_sensor_state);

  // perform reset
  if (reset) {
    // wait at least one second
    uint32_t ms = naos_millis();
    if (ms < 1100) {
      if (AL_SENSOR_DEBUG) {
        naos_log("al-sns: delay init by %dms", 1100 - ms);
      }
      naos_delay(1100 - ms);
    }

    // reset sensor
    al_sensor_hal_err_t err = al_sensor_hal_config(AL_SENSOR_HAL_NORMAL, 0);
    if (err != AL_SENSOR_HAL_OK) {
      naos_log("al-sns: HAL error=%d", err);
      ESP_ERROR_CHECK(ESP_FAIL);
    }

    // prepare compensation times
    al_sensor_switch_comp = al_clock_get_epoch();
    al_sensor_long_comp_last = al_clock_get_epoch();
    al_sensor_gas_last = 0;

    // initialize gas index parameters
    GasIndexAlgorithm_init_with_sampling_interval(&al_sensor_voc_params, GasIndexAlgorithm_ALGORITHM_TYPE_VOC,
                                                  AL_SENSOR_GAS_STEP / 1000.f);
    GasIndexAlgorithm_init_with_sampling_interval(&al_sensor_nox_params, GasIndexAlgorithm_ALGORITHM_TYPE_NOX,
                                                  AL_SENSOR_GAS_STEP / 1000.f);
  }

  // log state
  naos_log("al-sns: init mode=%d interval=%d", al_sensor_state.mode, al_sensor_state.interval);

  // ensure store is shifted once
  al_sensor_monitor();

  // ingest ULP readings
  naos_log("al-sns: ULP readings=%d", al_ulp_readings());
  for (int i = 0; i < al_ulp_readings(); i++) {
    al_sensor_ingest(al_ulp_get_reading(i));
  }

  // run check and monitor tasks
  naos_repeat("al-sns-c", 100, al_sensor_check);
  naos_repeat("al-sns-m", 1000, al_sensor_monitor);
}

void al_sensor_config(al_sensor_hook_t hook) {
  // set hook
  naos_lock(al_sensor_mutex);
  al_sensor_hook = hook;
  naos_unlock(al_sensor_mutex);
}

al_sample_t al_sensor_next() {
  // await signal
  naos_await(al_sensor_signal, 1, true, -1);

  // get last sample
  al_sample_t sample = al_store_last();

  return sample;
}

void al_sensor_sleep() {
  // acquire mutex
  naos_lock(al_sensor_mutex);

  // turn off the SGP heater in manual mode, so the ULP can duty-cycle it
  if (al_sensor_state.mode == AL_SENSOR_HAL_MANUAL) {
    al_sensor_hal_err_t err = al_sensor_hal_heater_off();
    if (err != AL_SENSOR_HAL_OK) {
      naos_log("al-sns: HAL error=%d", err);
    }
    al_sensor_state.heat = 0;
  }

  // release mutex
  naos_unlock(al_sensor_mutex);
}

void al_sensor_set_interval(int32_t seconds) {
  // clamp interval
  if (seconds < 5) {
    seconds = 5;
  } else if (seconds > 300) {
    seconds = 300;
  }

  // lock mutex
  naos_lock(al_sensor_mutex);

  // determine mode and interval
  al_sensor_hal_mode_t mode = AL_SENSOR_HAL_NORMAL;
  int interval = 0;
  if (seconds >= 60) {
    mode = AL_SENSOR_HAL_MANUAL;
    interval = seconds * 1000 - AL_SENSOR_MSR_TIME;
  } else if (seconds >= 30) {
    mode = AL_SENSOR_HAL_LOW_POWER;
  } else {
    mode = AL_SENSOR_HAL_NORMAL;
  }

  // skip if already set
  if (al_sensor_state.mode == mode && al_sensor_state.interval == interval) {
    naos_unlock(al_sensor_mutex);
    return;
  }

  // set mode and interval
  al_sensor_hal_err_t err = al_sensor_hal_config(mode, interval);
  if (err != AL_SENSOR_HAL_OK) {
    naos_log("al-sns: HAL error=%d", err);
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // reset switch compensation time
  al_sensor_switch_comp = al_clock_get_epoch();

  // log state
  naos_log("al-sns: config mode=%d interval=%d", mode, interval);

  // unlock mutex
  naos_unlock(al_sensor_mutex);
}

float al_sensor_raw_temp() {
  // return last raw temperature
  return al_sensor_last_raw_temp;
}

uint16_t al_sensor_raw_voc() {
  // return last raw VOC reading
  return al_sensor_last_raw_voc;
}

uint16_t al_sensor_raw_nox() {
  // return last raw NOx reading
  return al_sensor_last_raw_nox;
}
