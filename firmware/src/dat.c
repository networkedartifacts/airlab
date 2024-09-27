#include <naos.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#ifndef DAT_TEST
#include <esp_vfs_fat.h>
#include <esp_partition.h>
#include <tinyusb.h>
#include <tusb_msc_storage.h>
#else
#include <assert.h>
#define ESP_FAIL 1
#define ESP_ERROR_CHECK(x) assert(x == 0)
#endif

#include "dat.h"
#include "sys.h"
#include "tusb_tasks.h"

#ifdef DAT_TEST
#define DAT_ROOT "./fs"
#else
#define DAT_ROOT "/fs"
#endif

#define DAT_TAG "TAG.BIN"
#define DAT_COUNTER "COUNTER.BIN"
#define DAT_NAME_FMT "FILE%04u.BIN"

#define DAT_FILES 128
#define DAT_QUERY_BATCH 32

#define DAT_DEBUG false

static wl_handle_t dat_wl_handle;
static uint16_t dat_counter;
static dat_file_t *dat_files;
static size_t dat_files_length = 0;

// TODO: Handle file overflow.
// TODO: Only reference files by their number an not index.
// TODO: Explore need for high speed USB support.
// TODO: Support USB reset with VBus detection.

static tusb_desc_device_t dat_usb_dev_desc = {
    .bLength = sizeof(dat_usb_dev_desc),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_ESPRESSIF_VID,
    .idProduct = 0x4002,
    .bcdDevice = 0x100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static uint8_t const dat_usb_cfg_desc[] = {
    // config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(0, 0, 0x01, 0x81, 64),
};

static char const *dat_usb_str_desc[] = {
    (const char[]){0x09, 0x04},  // supported language is English (0x0409)
    "TinyUSB",                   // manufacturer
    "TinyUSB Device",            // product
    "123456",                    // serials
    "Example MSC",               // MSC
};

static void dat_usb_msc_cb(tinyusb_msc_event_t *event) {
  // log event
  naos_log("dat: MSC event=%d mounted=%d", event->type, event->mount_changed_data.is_mounted);
}

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

static void dat_read_file(const char *name, void *buf, size_t offset, size_t length) {
  // prepare path
  char path[32] = {0};
  strcat(path, DAT_ROOT "/");
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

static void dat_write_file(const char *name, void *buf, size_t offset, size_t length, bool truncate) {
  // prepare path
  char path[32] = {0};
  strcat(path, DAT_ROOT "/");
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

void dat_init() {
  // allocate files
  dat_files = calloc(DAT_FILES, sizeof(dat_file_t));

#ifndef DAT_TEST
  // mount FAT file system
  const esp_vfs_fat_mount_config_t mount_config = {
      .max_files = 2,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
  };
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_mount_rw_wl(DAT_ROOT, "storage", &mount_config, &dat_wl_handle));
#endif

  // check for tag
  if (access(DAT_ROOT "/" DAT_TAG, F_OK) != 0) {
    naos_log("dat: missing tag, formatting storage...");
    ESP_ERROR_CHECK(esp_vfs_fat_spiflash_format_rw_wl(DAT_ROOT, "storage"));
    dat_write_file(DAT_TAG, NULL, 0, 0, true);
    naos_log("dat: storage formatted!");
  }

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

    // log
    if (DAT_DEBUG) {
      naos_log("dat: found '%s'", entry->d_name);
    }

    // handle specials
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    // skip tag
    if (strcmp(entry->d_name, DAT_TAG) == 0) {
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

    // prepare path
    char path[32] = {0};
    strcat(path, DAT_ROOT "/");
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

    // count marks
    for (size_t i = 0; i < DAT_MARKS; i++) {
      if (file.head.marks[i] != 0) {
        file.marks++;
      } else {
        break;
      }
    }

    // add file
    dat_files[dat_files_length] = file;
    dat_files_length++;
  }

  // close directory
  closedir(dir);
}

size_t dat_num_files() { return dat_files_length; }

dat_file_t *dat_get_file(size_t num) { return &dat_files[num]; }

dat_info_t dat_info() {
  // get free FATFS clusters
  FATFS *fs;
  uint32_t free_clusters;
  FRESULT res = f_getfree(DAT_ROOT, &free_clusters, &fs);
  if (res != FR_OK) {
    ESP_ERROR_CHECK(res);
  }

  // calculate total and free sectors
  uint32_t total_sectors = (fs->n_fatent - 2) * fs->csize;
  uint32_t free_sectors = free_clusters * fs->csize;

  // calculate total and free bytes
  uint32_t total_bytes = total_sectors * CONFIG_WL_SECTOR_SIZE;
  uint32_t free_bytes = free_sectors * CONFIG_WL_SECTOR_SIZE;

  return (dat_info_t){
      .total = total_bytes,
      .free = free_bytes,
      .usage = (float)(total_bytes - free_bytes) / (float)total_bytes,
  };
}

uint16_t dat_next() { return dat_counter + 1; }

size_t dat_create(int64_t start) {
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

  return dat_files_length - 1;
}

void dat_mark(uint16_t num, int32_t offset) {
  // find file
  dat_file_t *file = dat_find_file(num, NULL);
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
  dat_write_file(name, &file->head, 0, sizeof(dat_head_t), false);
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

        // copy values
        points[i].co2 = batch[batch_pos].co2;
        points[i].tmp = batch[batch_pos].tmp;
        points[i].hum = batch[batch_pos].hum;
        points[i].voc = batch[batch_pos].voc;
        points[i].nox = batch[batch_pos].nox;

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
        points[i].voc = lerp(batch[batch_pos].voc, batch[batch_pos + 1].voc, factor);
        points[i].nox = lerp(batch[batch_pos].nox, batch[batch_pos + 1].nox, factor);

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

void dat_enable_usb() {
  // unmount storage
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_unmount_rw_wl(DAT_ROOT, dat_wl_handle));

  // find partition
  const esp_partition_t *data_partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
  if (data_partition == NULL) {
    ESP_ERROR_CHECK(ESP_ERR_NOT_FOUND);
  }

  // initialize wear levelling
  ESP_ERROR_CHECK(wl_mount(data_partition, &dat_wl_handle));

  // initialize USB mass storage
  const tinyusb_msc_spiflash_config_t config_spi = {
      .wl_handle = dat_wl_handle,
      .callback_mount_changed = dat_usb_msc_cb,
      .callback_premount_changed = dat_usb_msc_cb,
      .mount_config =
          {
              .max_files = 2,
              .format_if_mount_failed = true,
              .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
          },
  };
  ESP_ERROR_CHECK(tinyusb_msc_storage_init_spiflash(&config_spi));

  // initialize USB driver
  const tinyusb_config_t usb_cfg = {
      .device_descriptor = &dat_usb_dev_desc,
      .string_descriptor = dat_usb_str_desc,
      .string_descriptor_count = sizeof(dat_usb_str_desc) / sizeof(dat_usb_str_desc[0]),
      .configuration_descriptor = dat_usb_cfg_desc,
  };
  ESP_ERROR_CHECK(tinyusb_driver_install(&usb_cfg));
}

void dat_disable_usb() {
  // stop USB task
  ESP_ERROR_CHECK(tusb_stop_task());

  // uninstall driver
  ESP_ERROR_CHECK(tinyusb_driver_uninstall());

  // de-initialize USB mass storage
  tinyusb_msc_storage_deinit();

  // unmount wear levelling
  ESP_ERROR_CHECK(wl_unmount(dat_wl_handle));

  // remount storage
  const esp_vfs_fat_mount_config_t mount_config = {
      .max_files = 2,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
  };
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_mount_rw_wl(DAT_ROOT, "storage", &mount_config, &dat_wl_handle));
}

void dat_dump(const char *name, const void *data, size_t size) {
  // truncate and write file
  dat_write_file(name, (void *)data, 0, size, true);
}
