#include <naos.h>
#include <naos/sys.h>

#include <al/sensor.h>
#include <al/clock.h>

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

RTC_FAST_ATTR static int64_t al_sensor_store_epoch = 0;
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

  // update sampling interval
  // gas_voc_params.mSamplingInterval = input.delta;
  // gas_nox_params.mSamplingInterval = input.delta;

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
      .co2 = co2,
      .tmp = tmp,
      .hum = hum,
      .voc = (float)voc_index,
      .nox = (float)nox_index,
      .prs = prs,
  };
  if (AL_SENSOR_DEBUG) {
    naos_log("al-sns: ingest co2=%.0f tmp=%.1f hum=%.1f voc=%.1f nox=%.1f prs=%.0f ingest off=%d, epoch=%lld",
             al_sample_read(sample, AL_SENSOR_CO2), al_sample_read(sample, AL_SENSOR_TMP),
             al_sample_read(sample, AL_SENSOR_HUM), al_sample_read(sample, AL_SENSOR_VOC),
             al_sample_read(sample, AL_SENSOR_NOX), al_sample_read(sample, AL_SENSOR_PRS), sample.off, data.epoch);
  }

  // shift 5s sample to the 30s store, if necessary
  if (al_sensor_store_count_5s == AL_SENSOR_NUM_5S) {
    if (al_sensor_store_skip_5s > 0) {
      al_sensor_store_skip_5s--;
    } else {
      al_sample_t shift = al_sensor_store_5s[al_sensor_store_pos_5s];
      al_sensor_store_30s[al_sensor_store_pos_30s] = shift;
      al_sensor_store_pos_30s++;
      if (al_sensor_store_pos_30s >= AL_SENSOR_NUM_30S) {
        al_sensor_store_pos_30s = 0;
      }
      if (al_sensor_store_count_30s < AL_SENSOR_NUM_30S) {
        al_sensor_store_count_30s++;
      }
      al_sensor_store_skip_5s = 5;
    }
  }

  // add sample to 5s store
  al_sensor_store_5s[al_sensor_store_pos_5s] = sample;
  al_sensor_store_pos_5s++;
  if (al_sensor_store_pos_5s >= AL_SENSOR_NUM_5S) {
    al_sensor_store_pos_5s = 0;
  }
  if (al_sensor_store_count_5s < AL_SENSOR_NUM_5S) {
    al_sensor_store_count_5s++;
  }

  // log store count
  if (AL_SENSOR_DEBUG) {
    naos_log("al-sns: store 5s=%d 30s=%d", al_sensor_store_count_5s, al_sensor_store_count_30s);
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
      .epoch = al_clock_get_epoch,
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

  // get time
  int64_t now = al_clock_get_epoch();

  // handle zero store epoch
  if (al_sensor_store_epoch == 0) {
    al_sensor_store_epoch = now - 12 * 60 * 60 * 1000;
  }

  // handle outdated store epoch
  if (now - al_sensor_store_epoch > 24 * 60 * 60 * 1000) {
    // determine shift
    int64_t shift = (now - al_sensor_store_epoch) / 2;

    // update epoch
    al_sensor_store_epoch += shift;

    // update stores
    for (int i = 0; i < al_sensor_store_count_5s; i++) {
      al_sensor_store_5s[i].off -= (int32_t)shift;
    }
    for (int i = 0; i < al_sensor_store_count_30s; i++) {
      al_sensor_store_30s[i].off -= (int32_t)shift;
    }
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
  al_sensor_hook = hook;
}

al_sample_t al_sensor_first() {
  // get sample
  if (al_sensor_count(AL_SENSOR_30S) > 0) {
    return al_sensor_get(AL_SENSOR_30S, 0);
  } else {
    return al_sensor_get(AL_SENSOR_5S, 0);
  }
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

size_t al_sensor_count(al_sensor_store_t store) {
  // lock mutex
  naos_lock(al_sensor_mutex);

  // return store count
  size_t count = 0;
  if (store == AL_SENSOR_5S) {
    count = al_sensor_store_count_5s;
  } else {
    count = al_sensor_store_count_30s;
  }

  // unlock mutex
  naos_unlock(al_sensor_mutex);

  return count;
}

al_sample_t al_sensor_get(al_sensor_store_t store, int num) {
  // lock mutex
  naos_lock(al_sensor_mutex);

  // get store info
  al_sample_t *samples = al_sensor_store_5s;
  int count = al_sensor_store_count_5s;
  int pos = al_sensor_store_pos_5s;
  int length = AL_SENSOR_NUM_5S;
  if (store == AL_SENSOR_30S) {
    samples = al_sensor_store_30s;
    count = al_sensor_store_count_30s;
    pos = al_sensor_store_pos_30s;
    length = AL_SENSOR_NUM_30S;
  }

  // calculate absolute position
  if (num < 0) {
    num = count + num;
  }
  if (num >= count) {
    num = count - 1;
  }

  // calculate index
  size_t index = num;
  if (count == length) {
    index = (pos + num) % length;
  }

  // get sample
  al_sample_t sample = samples[index];

  // unlock mutex
  naos_unlock(al_sensor_mutex);

  return sample;
}

static size_t al_sensor_source_count(void *ctx) {
  // return cumulative count
  return al_sensor_count(AL_SENSOR_30S) + al_sensor_count(AL_SENSOR_5S);
}

static int64_t al_sensor_source_start(void *ctx) {
  // return epoch based on oldest sample
  return al_sensor_store_epoch + al_sensor_first().off;
}

static int32_t al_sensor_source_stop(void *ctx) {
  // return stop based on newest and oldest sample
  return al_sensor_last().off - al_sensor_first().off;
}

static void al_sensor_source_read(void *ctx, al_sample_t *samples, size_t num, size_t offset) {
  // get first sample
  al_sample_t first = al_sensor_first();

  // get 30s count
  int count_30s = (int)al_sensor_count(AL_SENSOR_30S);

  // read samples
  for (size_t i = 0; i < num; i++) {
    int index = (int)(offset + i);
    if (index < count_30s) {
      samples[i] = al_sensor_get(AL_SENSOR_30S, index);
    } else {
      samples[i] = al_sensor_get(AL_SENSOR_5S, index - count_30s);
    }
    samples[i].off -= first.off;
  }
}

al_sample_source_t al_sensor_source() {
  return (al_sample_source_t){
      .count = al_sensor_source_count,
      .start = al_sensor_source_start,
      .stop = al_sensor_source_stop,
      .read = al_sensor_source_read,
  };
}
