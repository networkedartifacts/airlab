#include <naos.h>
#include <naos/sys.h>
#include <driver/i2c.h>

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
static al_sensor_state_t al_sensor_history[AL_SENSOR_HIST] = {0};
static size_t al_sensor_pos = 0;
static GasIndexAlgorithmParams al_sensor_voc_params;
static GasIndexAlgorithmParams al_sensor_nox_params;
static al_sensor_hook_t al_sensor_hook;

static bool al_sensor_transfer(uint8_t target, uint8_t* wd, size_t wl, uint8_t* rd, size_t rl) {
  esp_err_t err = ESP_OK;
  if (wl > 0 && rl > 0) {
    err = i2c_master_write_read_device(I2C_NUM_0, target, wd, wl, rd, rl, 1000);
  } else if (wl > 0) {
    err = i2c_master_write_to_device(I2C_NUM_0, target, wd, wl, 1000);
  } else {
    err = i2c_master_read_from_device(I2C_NUM_0, target, rd, rl, 1000);
  }
  return err == ESP_OK;
}

static void al_sensor_debug(const char* msg) {
  // print message
  naos_log("sns: %s", msg);
}

static al_sensor_state_t al_sensor_ingest(al_sensor_raw_t raw) {
  // calculate ppm, °C, % rH
  float co2 = (float)raw.co2;
  float tmp = -45.f + 175.f * ((float)raw.tmp / (float)(UINT16_MAX));
  float hum = 100.f * ((float)raw.hum / (float)(UINT16_MAX));
  if (AL_SENSOR_DEBUG) {
    naos_log("sns: SCD values: co2=%.0f tmp=%.1f hum=%.1f", co2, tmp, hum);
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
    naos_log("sns: SGP values: voc=%d nox=%d", voc_index, nox_index);
  }

  // calculate pressure
  float prs = (float)raw.prs / 4096.f;
  if (AL_SENSOR_DEBUG) {
    naos_log("sns: LPS pressure: %.2f hPa", prs);
  }

  // advance
  al_sensor_pos++;
  if (al_sensor_pos >= AL_SENSOR_HIST) {
    al_sensor_pos = 0;
  }

  // create state
  al_sensor_state_t state = {
      .ok = true,
      .co2 = co2,
      .tmp = tmp,
      .hum = hum,
      .voc = (float)voc_index,
      .nox = (float)nox_index,
      .prs = prs,
  };

  // set state
  al_sensor_history[al_sensor_pos] = state;

  return state;
}

static void al_sensor_check() {
  for (;;) {
    // wait a second
    naos_delay(1000);

    // acquire mutex
    naos_lock(al_sensor_mutex);

    // check if SCD measurement is available
    if (!al_sensor_ready()) {
      naos_unlock(al_sensor_mutex);
      continue;
    }

    // read sensor
    al_sensor_raw_t raw;
    if (!al_sensor_read(&raw)) {
      ESP_ERROR_CHECK(ESP_FAIL);
    }

    // ingest sensor data
    al_sensor_state_t state = al_sensor_ingest(raw);

    // release mutex
    naos_unlock(al_sensor_mutex);

    // trigger signal
    naos_trigger(al_sensor_signal, 1, false);

    // dispatch event
    if (al_sensor_hook != NULL) {
      al_sensor_hook(state);
    }
  }
}

void al_sensor_init() {
  // create mutex and signal
  al_sensor_mutex = naos_mutex();
  al_sensor_signal = naos_signal();

  // wait at least one second
  uint32_t ms = naos_millis();
  if (ms < 1100) {
    if (AL_SENSOR_DEBUG) {
      naos_log("delay init by %dms", 1100 - ms);
    }
    naos_delay(1100 - ms);
  }

  // wire sensor
  al_sensor_wire((al_sensor_ops_t){
      .transfer = al_sensor_transfer,
      .delay = naos_delay,
      .debug = al_sensor_debug,
  });

  // reset sensor
  if (!al_sensor_reset()) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // initialize gas index parameters
  GasIndexAlgorithm_init_with_sampling_interval(&al_sensor_voc_params, GasIndexAlgorithm_ALGORITHM_TYPE_VOC, 5.f);
  GasIndexAlgorithm_init_with_sampling_interval(&al_sensor_nox_params, GasIndexAlgorithm_ALGORITHM_TYPE_NOX, 5.f);

  // check ULP readings
  naos_log("sns: ulp readings=%d", al_ulp_readings());

  // ingest ULP reading
  if (al_ulp_readings() > 0) {
    al_sensor_ingest(al_ulp_last_reading());
  }

  // run check task
  naos_run("sns", 8192, 1, al_sensor_check);
}

void al_sensor_config(al_sensor_hook_t hook) {
  // set hook
  al_sensor_hook = hook;
}

al_sensor_state_t al_sensor_get() {
  // get state
  naos_lock(al_sensor_mutex);
  al_sensor_state_t state = al_sensor_history[al_sensor_pos];
  naos_unlock(al_sensor_mutex);

  return state;
}

al_sensor_state_t al_sensor_next() {
  // await signal
  naos_await(al_sensor_signal, 1, true);

  // get state
  al_sensor_state_t state = al_sensor_get();

  return state;
}

al_sensor_hist_t al_sensor_query(al_sensor_mode_t mode) {
  // prepare history
  al_sensor_hist_t hist = {0};

  // copy values
  for (size_t i = 0; i < AL_SENSOR_HIST; i++) {
    size_t pos = (al_sensor_pos + 1 + i) % AL_SENSOR_HIST;
    switch (mode) {
      case AL_SENSOR_CO2:
        hist.values[i] = al_sensor_history[pos].co2;
        break;
      case AL_SENSOR_TMP:
        hist.values[i] = al_sensor_history[pos].tmp;
        break;
      case AL_SENSOR_HUM:
        hist.values[i] = al_sensor_history[pos].hum;
        break;
      case AL_SENSOR_VOC:
        hist.values[i] = al_sensor_history[pos].voc;
        break;
      case AL_SENSOR_NOX:
        hist.values[i] = al_sensor_history[pos].nox;
        break;
      case AL_SENSOR_PRS:
        hist.values[i] = al_sensor_history[pos].prs;
        break;
    }
  }

  // calculate min/max
  hist.min = 9999.f;
  for (size_t i = 0; i < AL_SENSOR_HIST; i++) {
    if (hist.values[i] > hist.max) {
      hist.max = hist.values[i];
    }
    if (hist.values[i] < hist.min) {
      hist.min = hist.values[i];
    }
  }

  return hist;
}
