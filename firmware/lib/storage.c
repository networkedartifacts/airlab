#include <errno.h>
#include <naos.h>
#include <esp_littlefs.h>
#include <esp_vfs_fat.h>
#include <esp_partition.h>
#include <tinyusb.h>
#include <tinyusb_msc.h>
#include <tinyusb_default_config.h>

#include <al/storage.h>

#define AL_STORAGE_INT_LABEL "internal"
#define AL_STORAGE_EXT_LABEL "external"
#define AL_STORAGE_DEBUG false

static wl_handle_t al_storage_wl_handle;
static tinyusb_msc_storage_handle_t al_storage_handle = NULL;
static al_storage_eject_t al_storage_eject = NULL;

static tusb_desc_device_t al_storage_usb_dev_desc = {
    .bLength = sizeof(al_storage_usb_dev_desc),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = TINYUSB_ESPRESSIF_VID,
    .idProduct = 0x4002,
    .bcdDevice = 0x100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static uint8_t const al_storage_usb_cfg_desc[] = {
    // config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(0, 0, 0x01, 0x81, 64),
};

static char const *al_storage_usb_str_desc[] = {
    (const char[]){0x09, 0x04},  // supported language is English (0x0409)
    "TinyUSB",                   // manufacturer
    "TinyUSB Device",            // product
    "123456",                    // serials
    "Example MSC",               // MSC
};

static void al_storage_usb_msc_cb(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg) {
  // log event
  if (AL_STORAGE_DEBUG) {
    naos_log("al-sto: MSC event=%d mounted=%d", event->id, event->mount_changed_data.is_mounted);
  }

  // dispatch eject event on device-side re-mount
  if (event->id == TINYUSB_MSC_EVENT_MOUNT_COMPLETE && !event->mount_changed_data.is_mounted) {
    if (al_storage_eject != NULL) {
      al_storage_eject();
    }
  }
}

void al_storage_usb_event_handler(tinyusb_event_t *event, void *arg) {
  switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
      naos_log("al-sto: USB attached");
      break;
    case TINYUSB_EVENT_DETACHED:
      naos_log("al-sto: USB detached");
      break;
    default:
      break;
  }
}

static bool al_storage_access(const char *path) {
  // open file
  FILE *file = fopen(path, "w");
  if (file == NULL) {
    naos_log("al-sto: failed to create test file, error=%d", errno);
    return false;
  }

  // close file
  fclose(file);

  // remove file
  int ret = remove(path);
  if (ret != 0) {
    naos_log("al-sto: failed to remove test file, error=%d", ret);
    return false;
  }

  return true;
}

void al_storage_init() {
  // mount LittleFS file system
  esp_vfs_littlefs_conf_t lfs_conf = {
      .base_path = AL_STORAGE_INTERNAL,
      .partition_label = AL_STORAGE_INT_LABEL,
      .format_if_mount_failed = true,
      .grow_on_mount = true,
  };
  ESP_ERROR_CHECK(esp_vfs_littlefs_register(&lfs_conf));

  // check access
  if (!al_storage_access(AL_STORAGE_INTERNAL "/TEST")) {
    naos_log("al-sto: no INTERNAL access, formatting...");
    ESP_ERROR_CHECK(esp_littlefs_format(AL_STORAGE_INT_LABEL));
  }

  // mount FAT file system
  const esp_vfs_fat_mount_config_t mount_config = {
      .max_files = 2,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
  };
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_mount_rw_wl(AL_STORAGE_EXTERNAL, AL_STORAGE_EXT_LABEL, &mount_config,
                                                   &al_storage_wl_handle));

  // check access
  if (!al_storage_access(AL_STORAGE_EXTERNAL "/TEST")) {
    naos_log("al-sto: no EXTERNAL access, formatting...");
    ESP_ERROR_CHECK(esp_vfs_fat_spiflash_format_rw_wl(AL_STORAGE_EXTERNAL, AL_STORAGE_EXT_LABEL));
  }

  // set label
  FRESULT res = f_setlabel("AIRLAB");
  if (res != FR_OK) {
    naos_log("al-sto: failed to set label, error=%d", res);
  }
}

al_storage_info_t al_storage_info(al_storage_type_t type) {
  // handle internal storage
  if (type == AL_STORAGE_INT) {
    size_t total = 0, used = 0;
    ESP_ERROR_CHECK(esp_littlefs_info(AL_STORAGE_INT_LABEL, &total, &used));
    return (al_storage_info_t){
        .total = total,
        .free = total - used,
        .usage = (float)used / (float)total,
    };
  }

  /* handle external storage */

  // get free external FATFS clusters
  FATFS *fs;
  uint32_t free_clusters;
  FRESULT res = f_getfree(AL_STORAGE_EXTERNAL, &free_clusters, &fs);
  if (res != FR_OK) {
    naos_log("al-sto: failed to get free clusters, error=%d", res);
    return (al_storage_info_t){0};
  }

  // calculate total and free sectors
  uint32_t total_sectors = (fs->n_fatent - 2) * fs->csize;
  uint32_t free_sectors = free_clusters * fs->csize;

  // calculate total and free bytes
  uint32_t total_bytes = total_sectors * CONFIG_WL_SECTOR_SIZE;
  uint32_t free_bytes = free_sectors * CONFIG_WL_SECTOR_SIZE;

  return (al_storage_info_t){
      .total = total_bytes,
      .free = free_bytes,
      .usage = (float)(total_bytes - free_bytes) / (float)total_bytes,
  };
}

void al_storage_enable_usb(al_storage_eject_t eject) {
  // set eject callback
  al_storage_eject = eject;

  // unmount storage
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_unmount_rw_wl(AL_STORAGE_EXTERNAL, al_storage_wl_handle));

  // find partition
  const esp_partition_t *data_partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, AL_STORAGE_EXT_LABEL);
  if (data_partition == NULL) {
    ESP_ERROR_CHECK(ESP_ERR_NOT_FOUND);
  }

  // initialize wear levelling
  ESP_ERROR_CHECK(wl_mount(data_partition, &al_storage_wl_handle));

  // install USB mass storage driver
  tinyusb_msc_driver_config_t drv_cfg = {
      .callback = al_storage_usb_msc_cb,
  };
  ESP_ERROR_CHECK(tinyusb_msc_install_driver(&drv_cfg));

  // initialize USB mass storage
  tinyusb_msc_storage_config_t config = {
      .medium.wl_handle = al_storage_wl_handle,
      .fat_fs =
          {
              .base_path = NULL,  // TODO: Set?
              .config.max_files = 2,
              .config.format_if_mount_failed = true,
              .config.allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
          },
  };
  ESP_ERROR_CHECK(tinyusb_msc_new_storage_spiflash(&config, &al_storage_handle));

  // initialize USB driver
  tinyusb_config_t usb_cfg = TINYUSB_DEFAULT_CONFIG(al_storage_usb_event_handler);
  usb_cfg.descriptor.device = &al_storage_usb_dev_desc;
  usb_cfg.descriptor.string = al_storage_usb_str_desc;
  usb_cfg.descriptor.string_count = sizeof(al_storage_usb_str_desc) / sizeof(al_storage_usb_str_desc[0]);
  usb_cfg.descriptor.full_speed_config = al_storage_usb_cfg_desc;
  usb_cfg.phy.self_powered = true;
  usb_cfg.phy.vbus_monitor_io = GPIO_NUM_18;
  ESP_ERROR_CHECK(tinyusb_driver_install(&usb_cfg));
}

void al_storage_disable_usb() {
  // uninstall driver
  ESP_ERROR_CHECK(tinyusb_driver_uninstall());

  // delete storage
  ESP_ERROR_CHECK(tinyusb_msc_delete_storage(al_storage_handle));

  // de-initialize USB mass storage
  ESP_ERROR_CHECK(tinyusb_msc_uninstall_driver());

  // unmount wear levelling
  ESP_ERROR_CHECK(wl_unmount(al_storage_wl_handle));

  // remount storage
  const esp_vfs_fat_mount_config_t mount_config = {
      .max_files = 2,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
  };
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_mount_rw_wl(AL_STORAGE_EXTERNAL, AL_STORAGE_EXT_LABEL, &mount_config,
                                                   &al_storage_wl_handle));
}

void al_storage_reset() {
  // format storage
  ESP_ERROR_CHECK(esp_littlefs_format(AL_STORAGE_INT_LABEL));
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_format_rw_wl(AL_STORAGE_EXTERNAL, AL_STORAGE_EXT_LABEL));
}
