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

AL_KEEP static GasIndexAlgorithmParams al_sensor_voc_params = {0};
AL_KEEP static GasIndexAlgorithmParams al_sensor_nox_params = {0};

RTC_FAST_ATTR static uint8_t al_sensor_store_pos_5s = 0;
RTC_FAST_ATTR static uint8_t al_sensor_store_pos_30s = 0;
RTC_FAST_ATTR static uint8_t al_sensor_store_count_5s = 0;
RTC_FAST_ATTR static uint8_t al_sensor_store_count_30s = 0;
RTC_FAST_ATTR static uint8_t al_sensor_store_skip_5s = 0;
RTC_FAST_ATTR static al_sample_t al_sensor_store_5s[AL_SENSOR_NUM_5S] = {0};
RTC_FAST_ATTR static al_sample_t al_sensor_store_30s[AL_SENSOR_NUM_30S] = {0};

static bool al_sensor_transfer(uint8_t target, uint8_t *wd, size_t wl, uint8_t *rd, size_t rl) {
  return al_i2c_transfer(target, wd, wl, rd, rl, 1000) == ESP_OK;
}

static void al_sensor_debug(const char *msg) {
  // print message
  naos_log("al-sns: HAL %s", msg);
}

static al_sample_t al_sensor_ingest(al_sensor_hal_data_t data) {
  // calculate ppm, °C, % rH
  float co2 = (float)data.co2;
  float tmp = -45.f + 175.f * ((float)data.tmp / (float)(UINT16_MAX));
  float hum = 100.f * ((float)data.hum / (float)(UINT16_MAX));
  if (AL_SENSOR_DEBUG) {
    naos_log("al-sns: SCD values: co2=%.0f tmp=%.1f hum=%.1f", co2, tmp, hum);
  }

  // update sampling interval
  // gas_voc_params.mSamplingInterval = input.delta;
  // gas_nox_params.mSamplingInterval = input.delta;

  // perform gas index calculation
  int32_t voc_index = 0;
  int32_t nox_index = 0;
  GasIndexAlgorithm_process(&al_sensor_voc_params, data.voc, &voc_index);
  GasIndexAlgorithm_process(&al_sensor_nox_params, data.nox, &nox_index);
  if (AL_SENSOR_DEBUG) {
    naos_log("al-sns: SGP values: voc=%d nox=%d", voc_index, nox_index);
  }

  // calculate pressure
  float prs = (float)data.prs / 4096.f;
  if (AL_SENSOR_DEBUG) {
    naos_log("al-sns: LPS pressure: %.2f hPa", prs);
  }

  // create sample
  al_sample_t sample = {
      .co2 = co2,
      .tmp = tmp,
      .hum = hum,
      .voc = (float)voc_index,
      .nox = (float)nox_index,
      .prs = prs,
  };

  // add sample to 5s store
  al_sensor_store_5s[al_sensor_store_pos_5s] = sample;
  al_sensor_store_pos_5s++;
  if (al_sensor_store_pos_5s >= AL_SENSOR_NUM_5S) {
    al_sensor_store_pos_5s = 0;
  }
  if (al_sensor_store_count_5s < AL_SENSOR_NUM_5S) {
    al_sensor_store_count_5s++;
  }

  // add sample to 30s store, if not skipped
  if (al_sensor_store_skip_5s == 0) {
    al_sensor_store_30s[al_sensor_store_pos_30s] = sample;
    al_sensor_store_pos_30s++;
    if (al_sensor_store_pos_30s >= AL_SENSOR_NUM_30S) {
      al_sensor_store_pos_30s = 0;
    }
    if (al_sensor_store_count_30s < AL_SENSOR_NUM_30S) {
      al_sensor_store_count_30s++;
    }
    al_sensor_store_skip_5s = 5;
  } else {
    al_sensor_store_skip_5s--;
  }

  // log store count
  if (AL_SENSOR_DEBUG) {
    naos_log("sns: store 5s=%d 30s=%d", al_sensor_store_count_5s, al_sensor_store_count_30s);
  }

  return sample;
}

static void al_sensor_check() {
  // acquire mutex
  naos_lock(al_sensor_mutex);

  // check if SCD measurement is available
  if (!al_sensor_hal_ready()) {
    naos_unlock(al_sensor_mutex);
    return;
  }

  // read sensor
  al_sensor_hal_data_t data;
  if (!al_sensor_hal_read(&data)) {
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

  // wire sensor HAL
  al_sensor_hal_wire((al_sensor_hal_ops_t){
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
    if (!al_sensor_hal_reset()) {
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

al_sample_t al_sensor_last() {
  // get sample
  naos_lock(al_sensor_mutex);
  int pos = al_sensor_store_pos_5s - 1;
  if (pos < 0) {
    pos = AL_SENSOR_NUM_5S - 1;
  }
  al_sample_t sample = al_sensor_store_5s[pos];
  naos_unlock(al_sensor_mutex);

  return sample;
}

al_sample_t al_sensor_next() {
  // await signal
  naos_await(al_sensor_signal, 1, true);

  // get last sample
  al_sample_t sample = al_sensor_last();

  return sample;
}

size_t al_sensor_count(al_sample_store_t store) {
  // return store count
  if (store == AL_SENSOR_5S) {
    return al_sensor_store_count_5s;
  } else {
    return al_sensor_store_count_30s;
  }
}

al_sample_t al_sensor_get(al_sample_store_t store, int num) {
  // get store info
  al_sample_t *samples = al_sensor_store_5s;
  int count = al_sensor_store_count_5s;
  int pos = al_sensor_store_pos_5s;
  if (store == AL_SENSOR_30S) {
    samples = al_sensor_store_30s;
    count = al_sensor_store_count_30s;
    pos = al_sensor_store_pos_30s;
  }

  // calculate absolute position
  if (num < 0) {
    num = count + num;
  }
  if (num >= count) {
    num = count - 1;
  }

  // calculate relative position
  size_t n = (pos + 1 + num) % count;

  return samples[n];
}

size_t al_sensor_query(al_sample_store_t store, al_sensor_t sensor, int num, float *values, float *min, float *max) {
  // limit number to count
  int count = (int)al_sensor_count(store);
  if (num > count) {
    num = count;
  }

  // prepare from/to indexes
  int from = 0;
  int to = num;
  if (num < 0) {
    from = num;
    to = 0;
  }

  // copy values
  for (int i = from; i < to; i++) {
    al_sample_t sample = al_sensor_get(store, i);
    switch (sensor) {
      case AL_SENSOR_CO2:
        values[i] = sample.co2;
        break;
      case AL_SENSOR_TMP:
        values[i] = sample.tmp;
        break;
      case AL_SENSOR_HUM:
        values[i] = sample.hum;
        break;
      case AL_SENSOR_VOC:
        values[i] = sample.voc;
        break;
      case AL_SENSOR_NOX:
        values[i] = sample.nox;
        break;
      case AL_SENSOR_PRS:
        values[i] = sample.prs;
        break;
    }
  }

  // calculate min/max
  if (min != NULL) {
    *min = 9999.f;
  }
  for (size_t i = 0; i < num; i++) {
    if (max != NULL && values[i] > *max) {
      *max = values[i];
    }
    if (min != NULL && values[i] < *min) {
      *min = values[i];
    }
  }

  return num;
}
