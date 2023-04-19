#include <naos_sys.h>

#include "rec.h"
#include "sys.h"
#include "sns.h"
#include "sig.h"

static naos_mutex_t rec_mutex = NULL;
static naos_task_t rec_handle = NULL;
static dat_file_t* rec_current = NULL;

static void rec_task() {
  for (;;) {
    // await next state
    sns_state_t state = sns_next();

    // get file
    naos_lock(rec_mutex);
    dat_file_t* file = rec_current;
    naos_unlock(rec_mutex);

    // calculate offset
    int64_t offset = sys_get_timestamp() - file->head.start;

    // prepare point
    dat_point_t point = {
        .offset = (int32_t)offset,
        .co2 = state.co2,
        .hum = state.hum,
        .tmp = state.tmp,
    };

    // append point
    dat_append(file->head.num, &point, 1);

    // dispatch event
    sig_dispatch(SIG_APPEND);
  }
}

void rec_init() {
  // create mutex
  rec_mutex = naos_mutex();
}

dat_file_t* rec_file() {
  // get file
  naos_lock(rec_mutex);
  dat_file_t* file = rec_current;
  naos_unlock(rec_mutex);

  return file;
}

bool rec_running() { return rec_file() != NULL; }

void rec_start(dat_file_t* file) {
  // check file
  if (rec_running()) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // set file
  naos_lock(rec_mutex);
  rec_current = file;
  naos_unlock(rec_mutex);

  // run task
  rec_handle = naos_run("rec", 4096, 1, rec_task);
}

void rec_stop() {
  // check file
  if (!rec_running()) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // clear file
  naos_lock(rec_mutex);
  rec_current = NULL;
  naos_unlock(rec_mutex);

  // kill task
  naos_kill(rec_handle);
  rec_handle = NULL;
}
