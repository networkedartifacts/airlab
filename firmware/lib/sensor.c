#include <naos.h>
#include <naos/sys.h>

#include <al/sensor.h>

#include "internal.h"
#include "sensor_hal.h"
#include "sensor_gas.h"

#define AL_SENSOR_DEBUG false

// TODO: Support low power measurement mode (30s).
// TODO: Perform SGP41 conditioning (10s).
// TODO: Update gas index sampling interval dynamically?

static naos_mutex_t al_sensor_mutex;
static naos_signal_t al_sensor_signal;
static al_sensor_hook_t al_sensor_hook;

AL_KEEP static size_t al_sensor_pos = 0;
AL_KEEP static al_sensor_sample_t al_sensor_samples[AL_SENSOR_HIST] = {0};
AL_KEEP static GasIndexAlgorithmParams al_sensor_voc_params = {0};
AL_KEEP static GasIndexAlgorithmParams al_sensor_nox_params = {0};

static bool al_sensor_transfer(uint8_t target, uint8_t* wd, size_t wl, uint8_t* rd, size_t rl) {
  return al_i2c_transfer(target, wd, wl, rd, rl, 1000) == ESP_OK;
}

static void al_sensor_debug(const char* msg) {
  // print message
  naos_log("al-sns: HAL %s", msg);
}

static al_sensor_sample_t al_sensor_ingest(al_sensor_raw_t raw) {
  // calculate ppm, °C, % rH
  float co2 = (float)raw.co2;
  float tmp = -45.f + 175.f * ((float)raw.tmp / (float)(UINT16_MAX));
  float hum = 100.f * ((float)raw.hum / (float)(UINT16_MAX));
  if (AL_SENSOR_DEBUG) {
    naos_log("al-sns: SCD values: co2=%.0f tmp=%.1f hum=%.1f", co2, tmp, hum);
  }

  // update sampling interval
  // gas_voc_params.mSamplingInterval = input.delta;
  // gas_nox_params.mSamplingInterval = input.delta;

  // perform gas index calculation
  int32_t voc_index = 0;
  int32_t nox_index = 0;
  GasIndexAlgorithm_process(&al_sensor_voc_params, raw.voc, &voc_index);
  GasIndexAlgorithm_process(&al_sensor_nox_params, raw.nox, &nox_index);
  if (AL_SENSOR_DEBUG) {
    naos_log("al-sns: SGP values: voc=%d nox=%d", voc_index, nox_index);
  }

  // calculate pressure
  float prs = (float)raw.prs / 4096.f;
  if (AL_SENSOR_DEBUG) {
    naos_log("al-sns: LPS pressure: %.2f hPa", prs);
  }

  // advance
  al_sensor_pos++;
  if (al_sensor_pos >= AL_SENSOR_HIST) {
    al_sensor_pos = 0;
  }

  // create sample
  al_sensor_sample_t sample = {
      .ok = true,
      .co2 = co2,
      .tmp = tmp,
      .hum = hum,
      .voc = (float)voc_index,
      .nox = (float)nox_index,
      .prs = prs,
  };

  // add sample
  al_sensor_samples[al_sensor_pos] = sample;

  return sample;
}

static void al_sensor_check() {
  // acquire mutex
  naos_lock(al_sensor_mutex);

  // check if SCD measurement is available
  if (!al_sensor_ready()) {
    naos_unlock(al_sensor_mutex);
    return;
  }

  // read sensor
  al_sensor_raw_t raw;
  if (!al_sensor_read(&raw)) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // ingest reading
  al_sensor_sample_t sample = al_sensor_ingest(raw);

  // release mutex
  naos_unlock(al_sensor_mutex);

  // trigger signal
  naos_trigger(al_sensor_signal, 1, false);

  // dispatch event
  if (al_sensor_hook != NULL) {
    al_sensor_hook(sample);
  }
}

void al_sensor_init(bool reset) {
  // create mutex and signal
  al_sensor_mutex = naos_mutex();
  al_sensor_signal = naos_signal();

  // wire sensor
  al_sensor_wire((al_sensor_ops_t){
      .transfer = al_sensor_transfer,
      .delay = naos_delay,
      .debug = al_sensor_debug,
  });

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
    if (!al_sensor_reset()) {
      ESP_ERROR_CHECK(ESP_FAIL);
    }

    // initialize gas index parameters
    GasIndexAlgorithm_init_with_sampling_interval(&al_sensor_voc_params, GasIndexAlgorithm_ALGORITHM_TYPE_VOC, 5.f);
    GasIndexAlgorithm_init_with_sampling_interval(&al_sensor_nox_params, GasIndexAlgorithm_ALGORITHM_TYPE_NOX, 5.f);
  }

  // ingest ULP readings
  naos_log("al-sns: ULP readings=%d", al_ulp_readings());
  for (int i = 0; i < al_ulp_readings(); i++) {
    al_sensor_ingest(al_ulp_get_reading(i));
  }

  // run check task
  naos_repeat("al-sns", 1000, al_sensor_check);
}

void al_sensor_config(al_sensor_hook_t hook) {
  // set hook
  al_sensor_hook = hook;
}

al_sensor_sample_t al_sensor_get() {
  // get sample
  naos_lock(al_sensor_mutex);
  al_sensor_sample_t sample = al_sensor_samples[al_sensor_pos];
  naos_unlock(al_sensor_mutex);

  return sample;
}

al_sensor_sample_t al_sensor_next() {
  // await signal
  naos_await(al_sensor_signal, 1, true);

  // get sample
  al_sensor_sample_t sample = al_sensor_get();

  return sample;
}

al_sensor_history_t al_sensor_query(al_sensor_t sensor) {
  // prepare history
  al_sensor_history_t history = {0};

  // copy values
  for (size_t i = 0; i < AL_SENSOR_HIST; i++) {
    size_t pos = (al_sensor_pos + 1 + i) % AL_SENSOR_HIST;
    switch (sensor) {
      case AL_SENSOR_CO2:
        history.values[i] = al_sensor_samples[pos].co2;
        break;
      case AL_SENSOR_TMP:
        history.values[i] = al_sensor_samples[pos].tmp;
        break;
      case AL_SENSOR_HUM:
        history.values[i] = al_sensor_samples[pos].hum;
        break;
      case AL_SENSOR_VOC:
        history.values[i] = al_sensor_samples[pos].voc;
        break;
      case AL_SENSOR_NOX:
        history.values[i] = al_sensor_samples[pos].nox;
        break;
      case AL_SENSOR_PRS:
        history.values[i] = al_sensor_samples[pos].prs;
        break;
    }
  }

  // calculate min/max
  history.min = 9999.f;
  for (size_t i = 0; i < AL_SENSOR_HIST; i++) {
    if (history.values[i] > history.max) {
      history.max = history.values[i];
    }
    if (history.values[i] < history.min) {
      history.min = history.values[i];
    }
  }

  return history;
}
