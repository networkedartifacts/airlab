#include <naos.h>
#include <naos/sys.h>

#include <al/sensor.h>
#include <al/clock.h>

#include "internal.h"
#include "sensor_hal.h"
#include "sensor_gas.h"

#define AL_SENSOR_DEBUG false

static naos_mutex_t al_sensor_mutex;
static naos_signal_t al_sensor_signal;
static al_sensor_hook_t al_sensor_hook;

AL_KEEP static GasIndexAlgorithmParams al_sensor_voc_params = {0};
AL_KEEP static GasIndexAlgorithmParams al_sensor_nox_params = {0};

RTC_FAST_ATTR static int64_t al_sensor_store_epoch = 0;
RTC_FAST_ATTR static int32_t al_sensor_store_interval = 60;  // 1min
RTC_FAST_ATTR static uint16_t al_sensor_store_pos_short = 0;
RTC_FAST_ATTR static uint16_t al_sensor_store_pos_long = 0;
RTC_FAST_ATTR static uint16_t al_sensor_store_count_short = 0;
RTC_FAST_ATTR static uint16_t al_sensor_store_count_long = 0;
RTC_FAST_ATTR static al_sample_t al_sensor_store_short[AL_SENSOR_NUM_SHORT] = {0};
RTC_FAST_ATTR static al_sample_t al_sensor_store_long[AL_SENSOR_NUM_LONG] = {0};

static bool al_sensor_transfer(uint8_t target, uint8_t *wd, size_t wl, uint8_t *rd, size_t rl) {
  return al_i2c_transfer(target, wd, wl, rd, rl, 1000) == ESP_OK;
}

static void al_sensor_debug(const char *msg) {
  // print message
  naos_log("al-sns: HAL %s", msg);
}

static size_t al_sensor_index(al_sensor_store_t store, int num) {
  // get store info
  int count = al_sensor_store_count_short;
  int pos = al_sensor_store_pos_short;
  int length = AL_SENSOR_NUM_SHORT;
  if (store == AL_SENSOR_LONG) {
    count = al_sensor_store_count_long;
    pos = al_sensor_store_pos_long;
    length = AL_SENSOR_NUM_LONG;
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

  return index;
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
    naos_log("al-sns: ingest co2=%d tmp=%d hum=%d voc=%d nox=%d prs=%d ingest off=%d epoch=%lld", sample.co2,
             sample.tmp, sample.hum, sample.voc, sample.nox, sample.prs, sample.off, data.epoch);
  }

  // check if short store is at capacity
  if (al_sensor_store_count_short == AL_SENSOR_NUM_SHORT) {
    // determine if we need to move a sample
    bool move = false;

    // always move a sample if long store is empty
    if (al_sensor_store_count_long == 0) {
      move = true;
    }

    // move sample if first short sample is older than last long sample
    if (al_sensor_store_count_long > 0) {
      al_sample_t last_long = al_sensor_store_long[al_sensor_index(AL_SENSOR_LONG, -1)];
      al_sample_t first_short = al_sensor_store_short[al_sensor_store_pos_short];
      naos_log("al-sns: diff=%d", first_short.off - last_long.off);
      if (first_short.off - last_long.off > al_sensor_store_interval * 1000) {
        move = true;
      }
    }

    // move sample if required
    if (move) {
      if (AL_SENSOR_DEBUG) {
        naos_log("al-sns: moved short to long");
      }
      al_sample_t first_short = al_sensor_store_short[al_sensor_store_pos_short];
      al_sensor_store_long[al_sensor_store_pos_long] = first_short;
      al_sensor_store_pos_long++;
      if (al_sensor_store_pos_long >= AL_SENSOR_NUM_LONG) {
        al_sensor_store_pos_long = 0;
      }
      if (al_sensor_store_count_long < AL_SENSOR_NUM_LONG) {
        al_sensor_store_count_long++;
      }
    }
  }

  // add sample to short store
  al_sensor_store_short[al_sensor_store_pos_short] = sample;
  al_sensor_store_pos_short++;
  if (al_sensor_store_pos_short >= AL_SENSOR_NUM_SHORT) {
    al_sensor_store_pos_short = 0;
  }
  if (al_sensor_store_count_short < AL_SENSOR_NUM_SHORT) {
    al_sensor_store_count_short++;
  }

  // log store count
  if (AL_SENSOR_DEBUG) {
    naos_log("al-sns: store short=%d long=%d", al_sensor_store_count_short, al_sensor_store_count_long);
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
    for (int i = 0; i < al_sensor_store_count_short; i++) {
      al_sensor_store_short[i].off -= (int32_t)shift;
    }
    for (int i = 0; i < al_sensor_store_count_long; i++) {
      al_sensor_store_long[i].off -= (int32_t)shift;
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
  naos_lock(al_sensor_mutex);
  al_sensor_hook = hook;
  naos_unlock(al_sensor_mutex);
}

int al_sensor_get_interval() {
  // get interval
  naos_lock(al_sensor_mutex);
  int interval = al_sensor_store_interval;
  naos_unlock(al_sensor_mutex);

  return interval;
}

void al_sensor_set_interval(int interval) {
  // set interval
  naos_lock(al_sensor_mutex);
  al_sensor_store_interval = interval;
  naos_unlock(al_sensor_mutex);
}

al_sample_t al_sensor_first() {
  // get sample
  if (al_sensor_count(AL_SENSOR_LONG) > 0) {
    return al_sensor_get(AL_SENSOR_LONG, 0);
  } else {
    return al_sensor_get(AL_SENSOR_SHORT, 0);
  }
}

al_sample_t al_sensor_last() {
  // get newest sample
  return al_sensor_get(AL_SENSOR_SHORT, -1);
}

al_sample_t al_sensor_next() {
  // TODO: Does not work with multiple tasks.

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
  if (store == AL_SENSOR_SHORT) {
    count = al_sensor_store_count_short;
  } else {
    count = al_sensor_store_count_long;
  }

  // unlock mutex
  naos_unlock(al_sensor_mutex);

  return count;
}

al_sample_t al_sensor_get(al_sensor_store_t store, int num) {
  // lock mutex
  naos_lock(al_sensor_mutex);

  // calculate index
  size_t index = al_sensor_index(store, num);

  // get sample
  al_sample_t sample;
  if (store == AL_SENSOR_SHORT) {
    sample = al_sensor_store_short[index];
  } else {
    sample = al_sensor_store_long[index];
  }

  // unlock mutex
  naos_unlock(al_sensor_mutex);

  return sample;
}

static size_t al_sensor_source_count(void *ctx) {
  // return cumulative count
  return al_sensor_count(AL_SENSOR_LONG) + al_sensor_count(AL_SENSOR_SHORT);
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

  // get long count
  int count_long = (int)al_sensor_count(AL_SENSOR_LONG);

  // read samples
  for (size_t i = 0; i < num; i++) {
    int index = (int)(offset + i);
    if (index < count_long) {
      samples[i] = al_sensor_get(AL_SENSOR_LONG, index);
    } else {
      samples[i] = al_sensor_get(AL_SENSOR_SHORT, index - count_long);
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
