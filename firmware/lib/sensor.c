#include <naos.h>
#include <naos/sys.h>

#include <al/sensor.h>
#include <al/clock.h>
#include <al/store.h>

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
AL_KEEP static int64_t al_sensor_store_epoch = 0;

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

static al_sample_t al_sensor_ingest(al_sensor_hal_data_t data) {
  // calculate ppm, °C, % rH
  float co2 = (float)data.co2;
  float tmp = -45.f + 175.f * ((float)data.tmp / (float)(UINT16_MAX));
  float hum = 100.f * ((float)data.hum / (float)(UINT16_MAX));

  // perform gas index calculation
  int32_t voc_index = 0;
  int32_t nox_index = 0;
  GasIndexAlgorithm_process(&al_sensor_voc_params, data.voc, &voc_index);
  GasIndexAlgorithm_process(&al_sensor_nox_params, data.nox, &nox_index);

  // calculate pressure
  float prs = (float)data.prs / 4096.f;

  // create sample
  al_sample_t sample = {
      .off = (int32_t)(data.epoch - al_sensor_store_epoch),
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

static void al_sensor_check() {
  // acquire mutex
  naos_lock(al_sensor_mutex);

  // exit low power mode after 5s
  al_sensor_hal_err_t err = 0;
  if (al_sensor_state.mode != AL_SENSOR_HAL_NORMAL && naos_millis() > 5000) {
    err = al_sensor_hal_config(AL_SENSOR_HAL_NORMAL, 0);
    if (err != AL_SENSOR_HAL_OK) {
      naos_log("al-sns: HAL error=%d", err);
      ESP_ERROR_CHECK(ESP_FAIL);
    }
    if (AL_SENSOR_DEBUG) {
      naos_log("al-sns: mode=normal");
    }
  }

  // check if SCD measurement is available
  err = al_sensor_hal_ready();
  if (err != AL_SENSOR_HAL_OK) {
    if (err != AL_SENSOR_HAL_BUSY) {
      naos_log("al-sns: HAL error=%d", err);
      ESP_ERROR_CHECK(ESP_FAIL);
    }
    naos_unlock(al_sensor_mutex);
    return;
  }

  // read sensor
  al_sensor_hal_data_t data;
  err = al_sensor_hal_read(&data);
  if (err != AL_SENSOR_HAL_OK) {
    naos_log("al-sns: HAL read error=%d", err);
    ESP_ERROR_CHECK(ESP_FAIL);
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

    // initialize gas index parameters
    GasIndexAlgorithm_init_with_sampling_interval(&al_sensor_voc_params, GasIndexAlgorithm_ALGORITHM_TYPE_VOC, 5.f);
    GasIndexAlgorithm_init_with_sampling_interval(&al_sensor_nox_params, GasIndexAlgorithm_ALGORITHM_TYPE_NOX, 5.f);
  }

  // get time
  int64_t now = al_clock_get_epoch();

  // handle zero store epoch
  if (al_sensor_store_epoch == 0) {
    al_sensor_store_epoch = now - 12 * 60 * 60 * 1000;
    al_store_set_epoch(al_sensor_store_epoch);
  }

  // handle outdated store epoch
  if (now - al_sensor_store_epoch > 24 * 60 * 60 * 1000) {
    // determine shift
    int64_t shift = (now - al_sensor_store_epoch) / 2;

    // update epoch
    al_sensor_store_epoch += shift;

    // update store epoch
    al_store_set_epoch(al_sensor_store_epoch);
  }

  // ingest ULP readings
  naos_log("al-sns: ULP readings=%d", al_ulp_readings());
  for (int i = 0; i < al_ulp_readings(); i++) {
    al_sensor_ingest(al_ulp_get_reading(i));
  }

  // run check task
  naos_repeat("al-sns", 100, al_sensor_check);
}

void al_sensor_config(al_sensor_hook_t hook) {
  // set hook
  naos_lock(al_sensor_mutex);
  al_sensor_hook = hook;
  naos_unlock(al_sensor_mutex);
}

al_sample_t al_sensor_next() {
  // TODO: Does not work with multiple tasks.

  // await signal
  naos_await(al_sensor_signal, 1, true);

  // get last sample
  al_sample_t sample = al_store_last();

  return sample;
}

void al_sensor_low_power(bool on, bool manual) {
  // lock mutex
  naos_lock(al_sensor_mutex);

  // determine mode
  al_sensor_hal_mode_t mode = AL_SENSOR_HAL_NORMAL;
  if (on) {
    mode = manual ? AL_SENSOR_HAL_MANUAL : AL_SENSOR_HAL_LOW_POWER;
  }

  // check mode
  if (al_sensor_state.mode == mode) {
    naos_unlock(al_sensor_mutex);
    return;
  }

  // set low power mode
  al_sensor_hal_err_t err = al_sensor_hal_config(mode, 15000);
  if (err != AL_SENSOR_HAL_OK) {
    naos_log("al-sns: HAL error=%d", err);
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // log state
  if (AL_SENSOR_DEBUG) {
    naos_log("al-sns: mode=%d", mode);
  }

  // unlock mutex
  naos_unlock(al_sensor_mutex);
}
