#include <naos.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <al/core.h>
#include <al/sensor.h>
#include <al/store.h>
#include <al/storage.h>
#include <esp_err.h>

#include "dat.h"
#include "sig.h"

#define DAT_DATA_DIR "data"
#define DAT_EXPORT_DIR "export"
#define DAT_DUMP_DIR "dump"
#define DAT_NAME_FMT "file-%04u.bin"
#define DAT_EXPORT_FMT "file-%04u.csv"
#define DAT_FILES 128
#define DAT_EXPORT_BUF 4096
#define DAT_DEBUG false

#define DAT_MIN(x, y) (((x) < (y)) ? (x) : (y))

static int32_t dat_counter = 0;
static dat_file_t *dat_files;
static size_t dat_files_length = 0;

static void dat_eject() {
  // dispatch eject signal
  sig_dispatch((sig_event_t){
      .type = SIG_EJECT,
  });
}

static naos_param_t dat_params[] = {
    {.name = "file-counter", .type = NAOS_LONG, .sync_l = &dat_counter, .default_l = 0},
};

void dat_init() {
  // register params
  for (size_t i = 0; i < sizeof(dat_params) / sizeof(naos_param_t); i++) {
    naos_register(&dat_params[i]);
  }

  // allocate files
  dat_files = al_calloc(DAT_FILES, sizeof(dat_file_t));

  // ensure directory
  mkdir(AL_STORAGE_INTERNAL "/" DAT_DATA_DIR, 0777);

  // clear list
  dat_files_length = 0;

  // open directory
  DIR *dir = opendir(AL_STORAGE_INTERNAL "/" DAT_DATA_DIR);
  if (dir == NULL) {
    ESP_ERROR_CHECK(errno);
  }

  // read directory
  for (;;) {
    // get entry
    struct dirent *entry = readdir(dir);
    if (entry == NULL) {
      break;
    }

    // log
    if (DAT_DEBUG) {
      naos_log("dat: found '%s'", entry->d_name);
    }

    // ignore non regular files
    if (entry->d_type != DT_REG) {
      continue;
    }

    // handle specials
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    /* otherwise, handle files "FILE0001.BIN" */

    // prepare path
    char path[32] = {0};
    strcat(path, AL_STORAGE_INTERNAL "/" DAT_DATA_DIR "/");
    strcat(path, entry->d_name);

    // stat file
    struct stat info = {0};
    int ret = stat(path, &info);
    if (ret != 0 && errno == ENOENT) {
      continue;
    } else if (ret != 0) {
      ESP_ERROR_CHECK(errno);
    }

    // read size
    size_t size = info.st_size;
    if (size < sizeof(dat_head_t)) {
      continue;
    }

    // read head
    dat_head_t head = {0};
    if (!al_storage_read(AL_STORAGE_INTERNAL "/" DAT_DATA_DIR, entry->d_name, &head, 0, sizeof(head))) {
      continue;
    }

    // prepare file
    dat_file_t file = {.head = head};
    file.size = (size - sizeof(dat_head_t)) / sizeof(al_sample_t);

    // read last sample and set stop if available
    if (file.size > 0) {
      al_sample_t sample;
      if (!al_storage_read(AL_STORAGE_INTERNAL "/" DAT_DATA_DIR, entry->d_name, &sample,
                           sizeof(dat_head_t) + (file.size - 1) * sizeof(al_sample_t), sizeof(al_sample_t))) {
        ESP_ERROR_CHECK(ESP_FAIL);
      }
      file.stop = sample.off;
    }

    // count marks
    for (size_t i = 0; i < DAT_MARKS; i++) {
      if (file.head.marks[i] != 0) {
        file.marks++;
      } else {
        break;
      }
    }

    // check size
    if (dat_files_length >= DAT_FILES) {
      ESP_ERROR_CHECK(ESP_FAIL);
    }

    // log file
    if (DAT_DEBUG) {
      naos_log("dat: add num=%u start=%lld size=%zu stop=%d marks=%d ", file.head.num, file.head.start, file.size,
               file.stop, file.marks);
    }

    // add file
    dat_files[dat_files_length] = file;
    dat_files_length++;
  }

  // close directory
  closedir(dir);
}

size_t dat_count() {
  // return number of files
  return dat_files_length;
}

dat_file_t *dat_get(size_t num) {
  // return file
  return &dat_files[num];
}

dat_file_t *dat_find(uint16_t num, int *index) {
  // find file
  for (int i = 0; i < dat_files_length; i++) {
    if (dat_files[i].head.num == num) {
      // set index if available
      if (index != NULL) {
        *index = i;
      }

      return &dat_files[i];
    }
  }

  return NULL;
}

uint16_t dat_next() {
  // return next number
  return dat_counter + 1;
}

uint16_t dat_create(int64_t start) {
  // increment counter
  int32_t counter = dat_counter + 1;
  naos_set_l("file-counter", counter);

  // prepare head
  dat_head_t head = {
      .num = counter,
      .start = start,
  };

  // log
  if (DAT_DEBUG) {
    naos_log("dat: create num=%u start=%lld", head.num, head.start);
  }

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_NAME_FMT, head.num);

  // write file
  al_storage_write(AL_STORAGE_INTERNAL "/" DAT_DATA_DIR, name, &head, 0, sizeof(head), true);

  // prepare file
  dat_file_t file = {.head = head};

  // add file
  dat_files[dat_files_length] = file;
  dat_files_length++;

  return head.num;
}

void dat_mark(uint16_t num, int32_t offset) {
  // find file
  dat_file_t *file = dat_find(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
    return;
  }

  // log
  if (DAT_DEBUG) {
    naos_log("dat: mark num=%u offset=%d", num, offset);
  }

  // check marks
  if (file->marks >= DAT_MARKS) {
    return;
  }

  // add mark
  file->head.marks[file->marks] = offset;
  file->marks++;

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_NAME_FMT, num);

  // update head
  al_storage_write(AL_STORAGE_INTERNAL "/" DAT_DATA_DIR, name, &file->head, 0, sizeof(dat_head_t), false);
}

void dat_append(uint16_t num, al_sample_t *samples, size_t count) {
  // find file
  dat_file_t *file = dat_find(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
    return;
  }

  // calculate offset
  size_t offset = sizeof(dat_head_t) + (file->size * sizeof(al_sample_t));

  // calculate length
  size_t length = sizeof(al_sample_t) * count;

  // log
  if (DAT_DEBUG) {
    naos_log("dat: append num=%u count=%zu offset=%zu length=%zu ", num, count, offset, length);
  }

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_NAME_FMT, num);

  // append samples
  al_storage_write(AL_STORAGE_INTERNAL "/" DAT_DATA_DIR, name, samples, offset, length, false);

  // update head
  file->size += count;

  // update head
  al_storage_write(AL_STORAGE_INTERNAL "/" DAT_DATA_DIR, name, &file->head, 0, sizeof(dat_head_t), false);

  // update file
  file->stop = samples[count - 1].off;
}

void dat_load(uint16_t num, al_sample_t *samples, size_t count, size_t start) {
  // find file
  dat_file_t *file = dat_find(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // calculate offset
  size_t offset = sizeof(dat_head_t) + (start * sizeof(al_sample_t));

  // calculate length
  size_t length = sizeof(al_sample_t) * count;

  // log
  if (DAT_DEBUG) {
    naos_log("dat: load num=%u count=%zu start=%zu offset=%zu length=%zu", num, count, start, offset, length);
  }

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_NAME_FMT, num);

  // read samples
  if (!al_storage_read(AL_STORAGE_INTERNAL "/" DAT_DATA_DIR, name, samples, offset, length)) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }
}

void dat_delete(uint16_t num) {
  // find file
  int index;
  if (!dat_find(num, &index)) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_NAME_FMT, num);

  // delete file
  al_storage_delete(AL_STORAGE_INTERNAL "/" DAT_DATA_DIR, name);

  // remove file from list
  dat_files_length--;
  for (int i = index; i < dat_files_length; i++) {
    dat_files[i] = dat_files[i + 1];
  }
}

static size_t dat_source_count(void *ctx) {
  // return size
  return ((dat_file_t *)ctx)->size;
}

static int64_t dat_source_start(void *ctx) {
  // return start
  return ((dat_file_t *)ctx)->head.start;
}

static int32_t dat_source_stop(void *ctx) {
  // return stop
  return ((dat_file_t *)ctx)->stop;
}

static void dat_source_read(void *ctx, al_sample_t *samples, size_t count, size_t offset) {
  // read samples
  dat_load(((dat_file_t *)ctx)->head.num, samples, count, offset);
}

al_sample_source_t dat_source(uint16_t num) {
  // find file
  dat_file_t *file = dat_find(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  return (al_sample_source_t){
      .ctx = file,
      .count = dat_source_count,
      .start = dat_source_start,
      .stop = dat_source_stop,
      .read = dat_source_read,
  };
}

bool dat_import(uint16_t num, int start, dat_progress_t progress) {
  // find file
  dat_file_t *file = dat_find(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
    return false;
  }

  // prepare source
  al_sample_source_t source = al_store_source();
  size_t count = source.count(source.ctx);

  // check required space
  size_t space = sizeof(dat_head_t) + (count * sizeof(al_sample_t)) + 1024;
  if (space > al_storage_info(AL_STORAGE_INT).free) {
    return false;
  }

  // start progress
  if (progress != NULL) {
    progress(0, count);
  }

  // append samples
  for (size_t i = start; i < count; i += 32) {
    // read samples
    size_t n = DAT_MIN(count - i, 32);
    al_sample_t samples[32];
    source.read(source.ctx, samples, n, i);
    int64_t base = source.start(source.ctx);

    // log
    if (DAT_DEBUG) {
      naos_log("dat: import num=%u i=%zu n=%zu count=%zu", num, i, n, count);
    }

    // adjust sample offsets to reference file start
    for (size_t j = 0; j < n; j++) {
      samples[j].off += (int32_t)(base - file->head.start);
    }

    // append samples
    dat_append(num, samples, n);

    // update progress
    if (progress != NULL) {
      progress(i + n, count);
    }
  }

  return true;
}

bool dat_export(uint16_t num, dat_progress_t progress) {
  // ensure directory
  mkdir(AL_STORAGE_EXTERNAL "/" DAT_EXPORT_DIR, 0777);

  // find file
  dat_file_t *file = dat_find(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
    return false;
  }

  // calculate size
  size_t size = sizeof(dat_head_t) + (file->size * sizeof(al_sample_t)) + 1024;

  // check space
  if (size > al_storage_info(AL_STORAGE_EXT).free) {
    return false;
  }

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_EXPORT_FMT, num);

  // write header
  const char *header = "time,co2,tmp,hum,voc,nox,prs\n";
  al_storage_write(AL_STORAGE_EXTERNAL "/" DAT_EXPORT_DIR, name, (void *)header, 0, strlen(header), true);

  // prepare pos
  size_t file_pos = strlen(header);

  // allocate buffer
  void *buffer = al_alloc(DAT_EXPORT_BUF);
  size_t buf_pos = 0;

  // start progress
  if (progress != NULL) {
    progress(0, file->size);
  }

  // write samples
  for (size_t i = 0; i < file->size; i += 32) {
    // read samples
    size_t n = DAT_MIN(file->size - i, 32);
    al_sample_t samples[32];
    dat_load(num, samples, n, i);

    // log
    if (DAT_DEBUG) {
      naos_log("dat: export num=%u i=%zu n=%zu pos=%zu size=%zu", num, i, n, file_pos, file->size);
    }

    // write samples
    for (size_t j = 0; j < n; j++) {
      // append line to buffer
      buf_pos += snprintf(buffer + buf_pos, DAT_EXPORT_BUF - buf_pos, "%lld,%.0f,%.2f,%.2f,%.0f,%.0f,%.0f\n",
                          file->head.start + (int64_t)samples[j].off, al_sample_read(samples[j], AL_SAMPLE_CO2),
                          al_sample_read(samples[j], AL_SAMPLE_TMP), al_sample_read(samples[j], AL_SAMPLE_HUM),
                          al_sample_read(samples[j], AL_SAMPLE_VOC), al_sample_read(samples[j], AL_SAMPLE_NOX),
                          al_sample_read(samples[j], AL_SAMPLE_PRS));

      // flush buffer if full
      if (buf_pos > DAT_EXPORT_BUF - 128) {
        al_storage_write(AL_STORAGE_EXTERNAL "/" DAT_EXPORT_DIR, name, buffer, file_pos, buf_pos, false);
        file_pos += buf_pos;
        buf_pos = 0;
      }

      // update progress
      if (progress != NULL) {
        progress(i + j, file->size);
      }
    }
  }

  // flush buffer
  if (buf_pos > 0) {
    al_storage_write(AL_STORAGE_EXTERNAL "/" DAT_EXPORT_DIR, name, buffer, file_pos, buf_pos, false);
  }

  // free buffer
  free(buffer);

  return true;
}

void dat_reset() {
  // reset storage
  al_storage_reset();

  // reset length
  dat_files_length = 0;
}

void dat_enable_usb() {
  // enable USB
  al_storage_enable_usb(dat_eject);
}

void dat_disable_usb() {
  // disable USB
  al_storage_disable_usb();
}

void dat_dump(const char *name, const void *data, size_t size) {
  // ensure directory
  mkdir(AL_STORAGE_EXTERNAL "/" DAT_DUMP_DIR, 0777);

  // truncate and write file
  al_storage_write(AL_STORAGE_EXTERNAL "/" DAT_DUMP_DIR, name, (void *)data, 0, size, true);
}
