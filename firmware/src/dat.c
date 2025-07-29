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
#define DAT_COUNTER "counter.bin"
#define DAT_NAME_FMT "file-%04u.bin"
#define DAT_EXPORT_FMT "file-%04u.csv"
#define DAT_FILES 128
#define DAT_DEBUG false

#define DAT_MIN(x, y) (((x) < (y)) ? (x) : (y))

static uint16_t dat_counter;
static dat_file_t *dat_files;
static size_t dat_files_length = 0;

// TODO: Handle file overflow.

static void dat_read_file(const char *dir, const char *name, void *buf, size_t offset, size_t length) {
  // prepare path
  char path[32] = {0};
  strcat(path, AL_STORAGE_ROOT "/");
  strcat(path, dir);
  strcat(path, "/");
  strcat(path, name);

  // log
  if (DAT_DEBUG) {
    naos_log("dat: read %s %d %d", path, offset, length);
  }

  // open file
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    ESP_ERROR_CHECK(errno);
  }

  // seek file
  if (offset > 0) {
    int ret = fseek(file, (long)offset, SEEK_SET);
    if (ret != 0) {
      ESP_ERROR_CHECK(errno);
    }
  }

  // read data
  size_t ret = fread(buf, 1, length, file);
  if (ret != length) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // close file
  fclose(file);
}

static void dat_write_file(const char *dir, const char *name, void *buf, size_t offset, size_t length, bool truncate) {
  // prepare path
  char path[32] = {0};
  strcat(path, AL_STORAGE_ROOT "/");
  strcat(path, dir);
  strcat(path, "/");
  strcat(path, name);

  // log
  if (DAT_DEBUG) {
    naos_log("dat: write %s %d %d %d", path, offset, length, truncate);
  }

  // open file
  FILE *file = fopen(path, offset == 0 && truncate ? "w" : "r+");
  if (file == NULL) {
    ESP_ERROR_CHECK(errno);
  }

  // seek file
  if (offset > 0) {
    int ret = fseek(file, (long)offset, SEEK_SET);
    if (ret != 0) {
      ESP_ERROR_CHECK(errno);
    }
  }

  // write data
  size_t ret = fwrite(buf, 1, length, file);
  if (ret != length) {
    ESP_ERROR_CHECK(errno);
  }

  // close file
  fclose(file);
}

static void dat_delete_file(const char *dir, const char *name) {
  // prepare path
  char path[32] = {0};
  strcat(path, AL_STORAGE_ROOT "/");
  strcat(path, dir);
  strcat(path, "/");
  strcat(path, name);

  // log
  if (DAT_DEBUG) {
    naos_log("dat: delete %s", path);
  }

  // remove file
  int ret = remove(path);
  if (ret != 0 && ret != ENOENT) {
    ESP_ERROR_CHECK(errno);
  }
}

static void dat_eject() {
  // dispatch eject signal
  sig_dispatch((sig_event_t){
      .type = SIG_EJECT,
  });
}

void dat_init() {
  // allocate files
  dat_files = al_calloc(DAT_FILES, sizeof(dat_file_t));
  if (dat_files == NULL) {
    ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
  }

  // ensure directory
  mkdir(AL_STORAGE_ROOT "/" DAT_DATA_DIR, 0777);

  // clear list
  dat_files_length = 0;

  // open directory
  DIR *dir = opendir(AL_STORAGE_ROOT "/" DAT_DATA_DIR);
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

    // handle specials
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    // handle counter
    if (strcmp(entry->d_name, DAT_COUNTER) == 0) {
      uint16_t counter = 0;
      dat_read_file(DAT_DATA_DIR, entry->d_name, (uint8_t *)&counter, 0, sizeof(counter));
      dat_counter = counter;
      continue;
    }

    /* otherwise, handle files "FILE0001.BIN" */

    // prepare path
    char path[32] = {0};
    strcat(path, AL_STORAGE_ROOT "/");
    strcat(path, DAT_DATA_DIR "/");
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
    dat_read_file(DAT_DATA_DIR, entry->d_name, &head, 0, sizeof(head));

    // prepare file
    dat_file_t file = {.head = head};
    file.size = (size - sizeof(dat_head_t)) / sizeof(al_sample_t);

    // read last sample and set stop if available
    if (file.size > 0) {
      al_sample_t sample;
      dat_read_file(DAT_DATA_DIR, entry->d_name, &sample, sizeof(dat_head_t) + (file.size - 1) * sizeof(al_sample_t),
                    sizeof(al_sample_t));
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
  // prepare head
  dat_head_t head = {
      .num = dat_counter + 1,
      .start = start,
  };

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_NAME_FMT, head.num);

  // write file
  dat_write_file(DAT_DATA_DIR, name, &head, 0, sizeof(head), true);

  // write counter
  dat_write_file(DAT_DATA_DIR, DAT_COUNTER, &head.num, 0, sizeof(head.num), true);

  // prepare file
  dat_file_t file = {.head = head};

  // add file
  dat_files[dat_files_length] = file;
  dat_files_length++;

  // set counter
  dat_counter = head.num;

  return head.num;
}

void dat_mark(uint16_t num, int32_t offset) {
  // find file
  dat_file_t *file = dat_find(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
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
  dat_write_file(DAT_DATA_DIR, name, &file->head, 0, sizeof(dat_head_t), false);
}

void dat_append(uint16_t num, al_sample_t *samples, size_t count) {
  // find file
  dat_file_t *file = dat_find(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // calculate offset
  size_t offset = sizeof(dat_head_t) + (file->size * sizeof(al_sample_t));

  // calculate length
  size_t length = sizeof(al_sample_t) * count;

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_NAME_FMT, num);

  // append samples
  dat_write_file(DAT_DATA_DIR, name, samples, offset, length, false);

  // update head
  file->size += count;

  // update head
  dat_write_file(DAT_DATA_DIR, name, &file->head, 0, sizeof(dat_head_t), false);

  // update file
  file->stop = samples[count - 1].off;
}

void dat_read(uint16_t num, al_sample_t *samples, size_t count, size_t start) {
  // find file
  dat_file_t *file = dat_find(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // calculate offset
  size_t offset = sizeof(dat_head_t) + (start * sizeof(al_sample_t));

  // calculate length
  size_t length = sizeof(al_sample_t) * count;

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_NAME_FMT, num);

  // read samples
  dat_read_file(DAT_DATA_DIR, name, samples, offset, length);
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
  dat_delete_file(DAT_DATA_DIR, name);

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
  dat_read(((dat_file_t *)ctx)->head.num, samples, count, offset);
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

bool dat_import(uint16_t num) {
  // find file
  dat_file_t *file = dat_find(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // prepare source
  al_sample_source_t source = al_store_source();

  // get source info
  size_t count = source.count(source.ctx);

  // calculate size
  size_t size = sizeof(dat_head_t) + (count * sizeof(al_sample_t)) + 1024;

  // check space
  if (size > al_storage_info().free) {
    return false;
  }

  // append samples
  for (size_t i = 0; i < count; i += 32) {
    // read samples
    size_t n = DAT_MIN(file->size - i, 32);
    al_sample_t samples[32];
    source.read(source.ctx, samples, n, i);

    // TODO: Improve writing speed or report progress.

    // append samples
    dat_append(num, samples, n);
  }

  return true;
}

bool dat_export(uint16_t num) {
  // ensure directory
  mkdir(AL_STORAGE_ROOT "/" DAT_EXPORT_DIR, 0777);

  // find file
  dat_file_t *file = dat_find(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // calculate size
  size_t size = sizeof(dat_head_t) + (file->size * sizeof(al_sample_t)) + 1024;

  // check space
  if (size > al_storage_info().free) {
    return false;
  }

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_EXPORT_FMT, num);

  // write header
  const char *header = "time,co2,tmp,hum,voc,nox,prs\n";
  dat_write_file(DAT_EXPORT_DIR, name, (void *)header, 0, strlen(header), true);

  // prepare pos
  size_t pos = strlen(header);

  // write samples
  for (size_t i = 0; i < file->size; i += 32) {
    // read samples
    size_t count = DAT_MIN(file->size - i, 32);
    al_sample_t samples[32];
    dat_read(num, samples, count, i);

    // TODO: Improve writing speed or report progress.

    // write samples
    for (size_t j = 0; j < count; j++) {
      // prepare line
      char line[64];
      snprintf(line, sizeof(line), "%lld,%.0f,%.2f,%.2f,%.0f,%.0f,%.0f\n", file->head.start + (int64_t)samples[j].off,
               al_sample_read(samples[j], AL_SAMPLE_CO2), al_sample_read(samples[j], AL_SAMPLE_TMP),
               al_sample_read(samples[j], AL_SAMPLE_HUM), al_sample_read(samples[j], AL_SAMPLE_VOC),
               al_sample_read(samples[j], AL_SAMPLE_NOX), al_sample_read(samples[j], AL_SAMPLE_PRS));

      // write line
      dat_write_file(DAT_EXPORT_DIR, name, (void *)line, pos, strlen(line), false);
      pos += strlen(line);
    }
  }

  return true;
}

void dat_reset() {
  // reset storage
  al_storage_reset();

  // reset counter and length
  dat_counter = 0;
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
  mkdir(AL_STORAGE_ROOT "/" DAT_DUMP_DIR, 0777);

  // truncate and write file
  dat_write_file(DAT_DUMP_DIR, name, (void *)data, 0, size, true);
}
