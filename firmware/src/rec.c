#include <naos/sys.h>

#include <al/sensor.h>

#include "rec.h"
#include "sys.h"
#include "sig.h"

#define REC_MIN_FREE_NEW (3 * CONFIG_WL_SECTOR_SIZE)
#define REC_MIN_FREE_CONT (2 * CONFIG_WL_SECTOR_SIZE)

static naos_mutex_t rec_mutex = NULL;
static naos_task_t rec_handle = NULL;
static uint16_t rec_current = 0;

static void rec_task() {
  // acquire mutex
  naos_lock(rec_mutex);

  for (;;) {
    // await next sample (without mutex)
    naos_unlock(rec_mutex);
    al_sample_t sample = al_sensor_next();
    naos_lock(rec_mutex);

    // check free space
    if (!rec_free(false)) {
      // clear state
      rec_handle = 0;
      rec_current = 0;

      // dispatch event
      sig_dispatch((sig_event_t){
          .type = SIG_STOP,
      });

      // release mutex
      naos_unlock(rec_mutex);

      return;
    }

    // find file
    dat_file_t* file = dat_find(rec_current, NULL);
    if (file == NULL) {
      ESP_ERROR_CHECK(ESP_FAIL);
    }

    // set offset
    int64_t offset = sys_get_timestamp() - file->head.start;
    sample.off = (int32_t)offset;

    // append sample
    dat_append(rec_current, &sample, 1);

    // dispatch event
    sig_dispatch((sig_event_t){
        .type = SIG_APPEND,
    });
  }
}

void rec_init() {
  // create mutex
  rec_mutex = naos_mutex();
}

uint32_t rec_free(bool new) {
  // get info
  dat_info_t info = dat_info();

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
  bool running = rec_handle != NULL;
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

  // acquire mutex
  naos_lock(rec_mutex);

  // set file
  rec_current = file;

  // run task
  rec_handle = naos_run("rec", 4096, 1, rec_task);

  // release mutex
  naos_unlock(rec_mutex);
}

void rec_mark() {
  // check file
  if (!rec_running()) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // acquire mutex
  naos_lock(rec_mutex);

  // find file
  dat_file_t* file = dat_find(rec_current, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // calculate offset
  int64_t offset = sys_get_timestamp() - file->head.start;

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

  // acquire mutex
  naos_lock(rec_mutex);

  // clear file
  rec_current = 0;

  // kill task
  naos_kill(rec_handle);
  rec_handle = NULL;

  // release mutex
  naos_unlock(rec_mutex);
}
