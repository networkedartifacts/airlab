#include <naos.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <esp_vfs_fat.h>
#include <esp_partition.h>
#include <tinyusb.h>
#include <tusb_msc_storage.h>

#include "dat.h"
#include "sig.h"

#define DAT_ROOT "/fs"
#define DAT_TAG "TAG.BIN"
#define DAT_COUNTER "COUNTER.BIN"
#define DAT_NAME_FMT "FILE%04u.BIN"
#define DAT_FILES 128
#define DAT_DEBUG false

static wl_handle_t dat_wl_handle;
static uint16_t dat_counter;
static dat_file_t *dat_files;
static size_t dat_files_length = 0;

// TODO: Handle file overflow.
// TODO: Explore need for high speed USB support.

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
  if (DAT_DEBUG) {
    naos_log("dat: MSC event=%d mounted=%d", event->type, event->mount_changed_data.is_mounted);
  }

  // dispatch eject event on device-side re-mount
  if (event->type == TINYUSB_MSC_EVENT_MOUNT_CHANGED && event->mount_changed_data.is_mounted) {
    sig_dispatch((sig_event_t){
        .type = SIG_EJECT,
    });
  }
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
  if (dat_files == NULL) {
    ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
  }

  // mount FAT file system
  const esp_vfs_fat_mount_config_t mount_config = {
      .max_files = 2,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
  };
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_mount_rw_wl(DAT_ROOT, "storage", &mount_config, &dat_wl_handle));

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
    file.size = (size - sizeof(dat_head_t)) / sizeof(al_sample_t);

    // read last sample and set stop if available
    if (file.size > 0) {
      al_sample_t sample;
      dat_read_file(entry->d_name, &sample, sizeof(dat_head_t) + (file.size - 1) * sizeof(al_sample_t),
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

  // clear index
  if (index != NULL) {
    *index = -1;
  }

  return NULL;
}

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
  dat_write_file(name, &head, 0, sizeof(head), true);

  // write counter
  dat_write_file(DAT_COUNTER, &head.num, 0, sizeof(head.num), true);

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
  dat_write_file(name, &file->head, 0, sizeof(dat_head_t), false);
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
  dat_write_file(name, samples, offset, length, false);

  // update head
  file->size += count;

  // update head
  dat_write_file(name, &file->head, 0, sizeof(dat_head_t), false);

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
  dat_read_file(name, samples, offset, length);
}

void dat_delete(uint16_t num) {
  // find file
  int index;
  dat_find(num, &index);
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

static size_t dat_source_count(void *ctx) {
  // return size
  return ((dat_file_t *)ctx)->size;
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
      .read = dat_source_read,
  };
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
      .self_powered = true,
      .vbus_monitor_io = GPIO_NUM_18,
  };
  ESP_ERROR_CHECK(tinyusb_driver_install(&usb_cfg));
}

void dat_disable_usb() {
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
