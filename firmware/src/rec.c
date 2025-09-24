#include <naos.h>
#include <naos/sys.h>
#include <esp_err.h>

#include <al/sensor.h>
#include <al/clock.h>
#include <al/storage.h>
#include <al/store.h>

#include "dev.h"
#include "rec.h"
#include "sig.h"
#include "dat.h"

#define REC_MIN_FREE_NEW (3 * 4096)
#define REC_MIN_FREE_CONT (2 * 4096)
#define REC_DEBUG true

DEV_KEEP static uint16_t rec_current = 0;

static naos_mutex_t rec_mutex = NULL;

static void rec_backfill() {
  // find file
  dat_file_t* file = dat_find(rec_current, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
    return;
  }

  // prepare store source
  al_sample_source_t source = al_store_source();
  int64_t start = source.start(source.ctx);
  int32_t stop = source.stop(source.ctx);
  size_t count = source.count(source.ctx);

  // calculate the source relative offset of the next sample
  int32_t offset = (int32_t)(file->head.start + (int64_t)file->stop + 1000 - start);

  // log debug info
  if (REC_DEBUG) {
    naos_log("rec_backfill: source start=%lld needle=%zu stop=%d", start, offset, stop);
  }

  // search for index of next sample
  int index = al_sample_search(&source, &offset);
  if (index < 0) {
    if (REC_DEBUG) {
      naos_log("rec_backfill: next sample not found");
    }
    return;
  }

  // log debug info
  if (REC_DEBUG) {
    naos_log("rec_backfill: found sample index=%d num=%d", index, count - index);
  }

  // import samples
  dat_import(rec_current, index, NULL);
}

static void rec_task() {
  // acquire mutex
  naos_lock(rec_mutex);

  for (;;) {
    // await next sample (without mutex)
    naos_unlock(rec_mutex);
    al_sample_t sample = al_sensor_next();
    int64_t base = al_store_get_base();
    naos_lock(rec_mutex);

    // skip if not running
    if (rec_current == 0) {
      continue;
    }

    // check free space
    if (!rec_free(false)) {
      // clear file
      rec_current = 0;

      // dispatch event
      sig_dispatch((sig_event_t){
          .type = SIG_STOP,
      });

      continue;
    }

    // find file
    dat_file_t* file = dat_find(rec_current, NULL);
    if (file == NULL) {
      ESP_ERROR_CHECK(ESP_FAIL);
      continue;
    }

    // adjust sample offset to reference file start
    sample.off += (int32_t)(base - file->head.start);

    // skip if sample is not newer than last recorded
    if (sample.off <= file->stop) {
      if (REC_DEBUG) {
        naos_log("rec_task: skip file=%d offset=%d stop=%d", rec_current, sample.off, file->stop);
      }
      continue;
    }

    // append sample
    dat_append(rec_current, &sample, 1);

    // log debug info
    if (REC_DEBUG) {
      naos_log("rec_task: append file=%d offset=%d stop=%d", rec_current, sample.off, file->stop);
    }

    // dispatch event
    sig_dispatch((sig_event_t){
        .type = SIG_APPEND,
    });
  }
}

void rec_init(bool reset) {
  // create mutex
  rec_mutex = naos_mutex();

  // backfill if not reset and recording
  if (!reset && rec_current != 0) {
    rec_backfill();
  }

  // run task
  naos_run("rec", 4096, 1, rec_task);
}

uint32_t rec_free(bool new) {
  // get info
  al_storage_info_t info = al_storage_info(AL_STORAGE_INT);

  // check free space
  if (info.free < (new ? REC_MIN_FREE_NEW : REC_MIN_FREE_CONT)) {
    return 0;
  }

  // adjust free space
  info.free -= new ? REC_MIN_FREE_NEW : REC_MIN_FREE_CONT;

  // calculate free samples
  uint32_t samples = info.free / sizeof(al_sample_t);

  return samples;
}

uint16_t rec_file() {
  // get file
  naos_lock(rec_mutex);
  uint16_t file = rec_current;
  naos_unlock(rec_mutex);

  return file;
}

bool rec_running() {
  // check handle
  naos_lock(rec_mutex);
  bool running = rec_current != 0;
  naos_unlock(rec_mutex);

  return running;
}

void rec_start(uint16_t file) {
  // check file
  if (rec_running()) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // check free space
  if (!rec_free(false)) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // set file
  naos_lock(rec_mutex);
  rec_current = file;
  naos_unlock(rec_mutex);
}

void rec_mark() {
  // check file
  if (!rec_running()) {
    ESP_ERROR_CHECK(ESP_FAIL);
    return;
  }

  // acquire mutex
  naos_lock(rec_mutex);

  // find file
  dat_file_t* file = dat_find(rec_current, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
    return;
  }

  // calculate offset
  int64_t offset = al_clock_get_epoch() - file->head.start;

  // mark offset
  dat_mark(rec_current, (int32_t)offset);

  // release mutex
  naos_unlock(rec_mutex);
}

void rec_stop() {
  // check file
  if (!rec_running()) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // clear file
  naos_lock(rec_mutex);
  rec_current = 0;
  naos_unlock(rec_mutex);
}
