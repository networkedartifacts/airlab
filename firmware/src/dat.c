#include <stdio.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#ifndef DAT_TEST
#include <esp_vfs_fat.h>
#include <esp_partition.h>
#else
#include <assert.h>
#define ESP_FAIL 1
#define ESP_ERROR_CHECK(x) assert(x == 0)
#endif

#include "dat.h"
#include "sys.h"

#ifdef DAT_TEST
#define DAT_ROOT "./fs"
#else
#define DAT_ROOT "/fs"
#endif

#define DAT_COUNTER "COUNTER.BIN"
#define DAT_NAME_FMT "FILE%04u.BIN"

#define DAT_QUERY_BATCH 32

static uint16_t dat_counter;
static dat_file_t dat_files[128] = {0};
static size_t dat_files_length = 0;

float lerp(float a, float b, float f) { return a * (1.f - f) + (b * f); }

static void dat_format_file(dat_file_t *file) {
  // format title
  snprintf(file->title, sizeof(file->title), "Messung %u", file->head.num);

  // format date
  time_t time = (time_t)(file->head.start / 1000);
  struct tm ts = *gmtime(&time);
  strftime(file->date, sizeof(file->date), "%d.%m.%Y", &ts);
}

static dat_file_t *dat_find_file(uint16_t num, int *index) {
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

  // clear index
  *index = -1;

  return NULL;
}

static size_t dat_read_size(const char *name) {
  // prepare path
  char path[32] = {0};
  strcat(path, DAT_ROOT "/");
  strcat(path, name);

  // stat file
  struct stat info = {0};
  int ret = stat(path, &info);
  if (ret != 0) {
    ESP_ERROR_CHECK(errno);
  }

  return info.st_size;
}

static void dat_read_file(const char *name, void *buf, size_t offset, size_t length) {
  // prepare path
  char path[32] = {0};
  strcat(path, DAT_ROOT "/");
  strcat(path, name);

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

static void dat_write_file(const char *name, void *buf, size_t offset, size_t length, bool truncate) {
  // prepare path
  char path[32] = {0};
  strcat(path, DAT_ROOT "/");
  strcat(path, name);

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
  if (ret != 0) {
    ESP_ERROR_CHECK(errno);
  }

  // close file
  fclose(file);
}

static void dat_delete_file(const char *name) {
  // prepare path
  char path[32] = {0};
  strcat(path, DAT_ROOT "/");
  strcat(path, name);

  // remove file
  int ret = remove(path);
  if (ret != 0) {
    ESP_ERROR_CHECK(errno);
  }
}

void dat_init() {
#ifndef DAT_TEST
  // mount FAT file system
  wl_handle_t wl_handle;
  const esp_vfs_fat_mount_config_t mount_config = {
      .max_files = 2,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
  };
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_mount(DAT_ROOT, "storage", &mount_config, &wl_handle));
#endif

  // clear list
  dat_files_length = 0;

  // open directory
  DIR *dir = opendir(DAT_ROOT);
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

    // handle specials
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    // handle counter
    if (strcmp(entry->d_name, DAT_COUNTER) == 0) {
      uint16_t counter = 0;
      dat_read_file(entry->d_name, (uint8_t *)&counter, 0, sizeof(counter));
      dat_counter = counter;
      continue;
    }

    /* otherwise, handle files "FILE0001.BIN" */

    // read size
    size_t size = dat_read_size(entry->d_name);

    // read head
    dat_head_t head = {0};
    dat_read_file(entry->d_name, &head, 0, sizeof(head));

    // prepare file
    dat_file_t file = {.head = head};
    file.size = (size - sizeof(dat_head_t)) / sizeof(dat_point_t);
    dat_format_file(&file);

    // read last point and set stop if available
    if (file.size > 0) {
      dat_point_t point;
      dat_read_file(entry->d_name, &point, sizeof(dat_head_t) + (file.size - 1) * sizeof(dat_point_t),
                    sizeof(dat_point_t));
      file.stop = point.offset;
    }

    // add file
    dat_files[dat_files_length] = file;
    dat_files_length++;
  }

  // close directory
  closedir(dir);
}

size_t dat_num_files() { return dat_files_length; }

dat_file_t *dat_file_list() { return dat_files; }

uint16_t dat_next() { return dat_counter + 1; }

dat_file_t *dat_create(int64_t start) {
  // prepare head
  dat_head_t head = {
      .num = dat_counter + 1,
      .start = start,
  };

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_NAME_FMT, head.num);

  // write file
  dat_write_file(name, &head, 0, sizeof(head), true);

  // write counter
  dat_write_file(DAT_COUNTER, &head.num, 0, sizeof(head.num), true);

  // prepare file
  dat_file_t file = {.head = head};
  dat_format_file(&file);

  // add file
  dat_files[dat_files_length] = file;
  dat_files_length++;

  // set counter
  dat_counter = head.num;

  return &dat_files[dat_files_length - 1];
}

void dat_append(uint16_t num, dat_point_t *points, size_t count) {
  // find file
  dat_file_t *file = dat_find_file(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // calculate offset
  size_t offset = sizeof(dat_head_t) + (file->size * sizeof(dat_point_t));

  // calculate length
  size_t length = sizeof(dat_point_t) * count;

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_NAME_FMT, num);

  // append points
  dat_write_file(name, points, offset, length, false);

  // update head
  file->size += count;

  // update head
  dat_write_file(name, &file->head, 0, sizeof(dat_head_t), false);

  // update file
  file->stop = points[count - 1].offset;
}

void dat_read(uint16_t num, dat_point_t *points, size_t count, size_t start) {
  // find file
  dat_file_t *file = dat_find_file(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // calculate offset
  size_t offset = sizeof(dat_head_t) + (start * sizeof(dat_point_t));

  // calculate length
  size_t length = sizeof(dat_point_t) * count;

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_NAME_FMT, num);

  // read points
  dat_read_file(name, points, offset, length);
}

void dat_delete(uint16_t num) {
  // find file
  int index;
  dat_find_file(num, &index);
  if (index == -1) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // encode name
  char name[32];
  snprintf(name, sizeof(name), DAT_NAME_FMT, num);

  // delete file
  dat_delete_file(name);

  // remove file from list
  dat_files_length--;
  for (int i = index; i < dat_files_length; i++) {
    dat_files[i] = dat_files[i + 1];
  }
}

size_t dat_search(uint16_t num, int32_t *needle) {
  // find file
  dat_file_t *file = dat_find_file(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // calculate range
  size_t start = 0;
  size_t end = file->size - 1;

  // prepare point
  dat_point_t point;

  // find first offset to be greater or equal to needle using binary search
  while (start <= end) {
    // determine middle
    size_t middle = (start + end) / 2;

    // read point
    dat_read(num, &point, 1, middle);

    // handle result
    if (point.offset < *needle) {
      start = middle + 1;
      if (start >= file->size) {
        return -1;
      }
    } else {
      if (middle == 0) {
        return 0;
      }
      end = middle - 1;
    }
  }

  // update needle
  *needle = point.offset;

  return start;
}

size_t dat_query(uint16_t num, dat_point_t *points, size_t count, int32_t start, int32_t resolution) {
  // zero points
  memset(points, 0, count * sizeof(dat_point_t));

  // find file
  dat_file_t *file = dat_find_file(num, NULL);
  if (file == NULL) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // find beginning of range
  int32_t needle = start;
  size_t index = dat_search(num, &needle);
  if (needle > start) {
    index--;
  }

  // prepare batch
  dat_point_t batch[DAT_QUERY_BATCH];
  size_t batch_pos = 0;
  size_t batch_size = 0;

  // TODO: We need to retain marks in the range.
  // TODO: Also we only should a mark retain once.

  // fill points
  int32_t offset = start;
  for (size_t i = 0; i < count; i++) {
    // find next exact or range match
    for (;;) {
      // fill batch
      if (batch_size == 0 || batch_pos >= batch_size - 1) {
        // get length
        size_t length = file->size - index;
        if (length > DAT_QUERY_BATCH) {
          length = DAT_QUERY_BATCH;
        } else if (length == 0) {
          return i;
        }

        // read batch
        dat_read(num, batch, length, index);
        batch_pos = 0;
        batch_size = length;
      }

      // handle exact match
      if (batch[batch_pos].offset == offset) {
        // set offset
        points[i].offset = offset;

        // copy point
        points[i] = batch[batch_pos];

        break;
      }

      // handle range match
      if (batch[batch_pos + 1].offset > offset) {
        // set offset
        points[i].offset = offset;

        // calculate factor
        float factor = 1.f / (float)(batch[batch_pos + 1].offset - batch[batch_pos].offset) *
                       (float)(offset - batch[batch_pos].offset);

        // interpolate values
        points[i].co2 = lerp(batch[batch_pos].co2, batch[batch_pos + 1].co2, factor);
        points[i].tmp = lerp(batch[batch_pos].tmp, batch[batch_pos + 1].tmp, factor);
        points[i].hum = lerp(batch[batch_pos].hum, batch[batch_pos + 1].hum, factor);

        // copy mark
        points[i].mark = batch[batch_pos + 1].mark ? batch[batch_pos + 1].mark : batch[batch_pos].mark;

        break;
      }

      // advanced
      index++;
      batch_pos++;
    }

    // increment
    offset += resolution;
  }

  return count;
}

void dat_reset() {
  // open directory
  DIR *dir = opendir(DAT_ROOT);
  if (dir == NULL) {
    ESP_ERROR_CHECK(errno);
  }

  // read directory entries
  for (;;) {
    struct dirent *entry = readdir(dir);
    if (entry == NULL) {
      break;
    }
    dat_delete_file(entry->d_name);
  }

  // close directory
  closedir(dir);

  // reset counter and length
  dat_counter = 0;
  dat_files_length = 0;
}
