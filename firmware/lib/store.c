#include <naos.h>
#include <naos/sys.h>
#include <esp_attr.h>
#include <esp_err.h>

#include <al/store.h>

#define AL_STORE_DEBUG false

static naos_mutex_t al_store_mutex;

RTC_FAST_ATTR static int32_t al_store_interval = 60;  // 1min
RTC_FAST_ATTR static uint16_t al_store_pos_short = 0;
RTC_FAST_ATTR static uint16_t al_store_pos_long = 0;
RTC_FAST_ATTR static uint16_t al_store_count_short = 0;
RTC_FAST_ATTR static uint16_t al_store_count_long = 0;
RTC_FAST_ATTR static al_sample_t al_store_short[AL_STORE_NUM_SHORT] = {0};
RTC_FAST_ATTR static al_sample_t al_store_long[AL_STORE_NUM_LONG] = {0};
RTC_FAST_ATTR static int64_t al_store_epoch = 0;

static size_t al_store_index(al_store_t store, int num) {
  // get store info
  int count = al_store_count_short;
  int pos = al_store_pos_short;
  int length = AL_STORE_NUM_SHORT;
  if (store == AL_STORE_LONG) {
    count = al_store_count_long;
    pos = al_store_pos_long;
    length = AL_STORE_NUM_LONG;
  }

  // check count
  if (count == 0) {
    return 0;
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

void al_store_init() {
  // create mutex
  al_store_mutex = naos_mutex();
}

int al_store_get_interval() {
  // get interval
  naos_lock(al_store_mutex);
  int interval = al_store_interval;
  naos_unlock(al_store_mutex);

  return interval;
}

void al_store_set_interval(int interval) {
  // set interval
  naos_lock(al_store_mutex);
  al_store_interval = interval;
  naos_unlock(al_store_mutex);
}

int64_t al_store_get_epoch() {
  // get base
  naos_lock(al_store_mutex);
  int64_t base = al_store_epoch;
  naos_unlock(al_store_mutex);

  return base;
}

void al_store_set_epoch(int64_t base) {
  // lock mutex
  naos_lock(al_store_mutex);

  // determine shift
  int64_t shift = base - al_store_epoch;
  if (shift < 0) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // set base
  al_store_epoch = base;

  // update stores
  for (int i = 0; i < al_store_count_short; i++) {
    al_store_short[i].off -= (int32_t)shift;
  }
  for (int i = 0; i < al_store_count_long; i++) {
    al_store_long[i].off -= (int32_t)shift;
  }

  // unlock mutex
  naos_unlock(al_store_mutex);
}

void al_store_ingest(al_sample_t sample) {
  // lock mutex
  naos_lock(al_store_mutex);

  // check if short store is at capacity
  if (al_store_count_short == AL_STORE_NUM_SHORT) {
    // determine if we need to move a sample
    bool move = false;

    // always move a sample if long store is empty
    if (al_store_count_long == 0) {
      move = true;
    }

    // move sample if first short sample is older than last long sample
    if (al_store_count_long > 0) {
      al_sample_t last_long = al_store_long[al_store_index(AL_STORE_LONG, -1)];
      al_sample_t first_short = al_store_short[al_store_pos_short];
      if (AL_STORE_DEBUG) {
        naos_log("al-str: diff=%d", first_short.off - last_long.off);
      }
      if (first_short.off - last_long.off > al_store_interval * 1000) {
        move = true;
      }
    }

    // move sample if required
    if (move) {
      if (AL_STORE_DEBUG) {
        naos_log("al-str: moved short to long");
      }
      al_sample_t first_short = al_store_short[al_store_pos_short];
      al_store_long[al_store_pos_long] = first_short;
      al_store_pos_long++;
      if (al_store_pos_long >= AL_STORE_NUM_LONG) {
        al_store_pos_long = 0;
      }
      if (al_store_count_long < AL_STORE_NUM_LONG) {
        al_store_count_long++;
      }
    }
  }

  // add sample to short store
  al_store_short[al_store_pos_short] = sample;
  al_store_pos_short++;
  if (al_store_pos_short >= AL_STORE_NUM_SHORT) {
    al_store_pos_short = 0;
  }
  if (al_store_count_short < AL_STORE_NUM_SHORT) {
    al_store_count_short++;
  }

  // log store count
  if (AL_STORE_DEBUG) {
    naos_log("al-str: store short=%d long=%d", al_store_count_short, al_store_count_long);
  }

  // unlock mutex
  naos_unlock(al_store_mutex);
}

al_sample_t al_store_first() {
  // get sample
  if (al_store_count(AL_STORE_LONG) > 0) {
    return al_store_get(AL_STORE_LONG, 0);
  } else {
    return al_store_get(AL_STORE_SHORT, 0);
  }
}

al_sample_t al_store_last() {
  // get newest sample
  return al_store_get(AL_STORE_SHORT, -1);
}

size_t al_store_count(al_store_t store) {
  // lock mutex
  naos_lock(al_store_mutex);

  // return store count
  size_t count = 0;
  if (store == AL_STORE_SHORT) {
    count = al_store_count_short;
  } else {
    count = al_store_count_long;
  }

  // unlock mutex
  naos_unlock(al_store_mutex);

  return count;
}

al_sample_t al_store_get(al_store_t store, int num) {
  // lock mutex
  naos_lock(al_store_mutex);

  // calculate index
  size_t index = al_store_index(store, num);

  // get sample
  al_sample_t sample;
  if (store == AL_STORE_SHORT) {
    sample = al_store_short[index];
  } else {
    sample = al_store_long[index];
  }

  // unlock mutex
  naos_unlock(al_store_mutex);

  return sample;
}

static size_t al_store_source_count(void *ctx) {
  // return cumulative count
  return al_store_count(AL_STORE_LONG) + al_store_count(AL_STORE_SHORT);
}

static int64_t al_store_source_start(void *ctx) {
  // return epoch based on oldest sample
  return al_store_epoch + al_store_first().off;
}

static int32_t al_store_source_stop(void *ctx) {
  // return stop based on newest and oldest sample
  return al_store_last().off - al_store_first().off;
}

static void al_store_source_read(void *ctx, al_sample_t *samples, size_t num, size_t offset) {
  // get first sample
  al_sample_t first = al_store_first();

  // get long count
  int count_long = (int)al_store_count(AL_STORE_LONG);

  // read samples
  for (size_t i = 0; i < num; i++) {
    int index = (int)(offset + i);
    if (index < count_long) {
      samples[i] = al_store_get(AL_STORE_LONG, index);
    } else {
      samples[i] = al_store_get(AL_STORE_SHORT, index - count_long);
    }
    samples[i].off -= first.off;
  }
}

al_sample_source_t al_store_source() {
  return (al_sample_source_t){
      .count = al_store_source_count,
      .start = al_store_source_start,
      .stop = al_store_source_stop,
      .read = al_store_source_read,
  };
}
