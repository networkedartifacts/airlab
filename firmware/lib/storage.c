#include <naos.h>
#include <esp_vfs_fat.h>
#include <esp_partition.h>
#include <tinyusb.h>
#include <tusb_msc_storage.h>

#include <al/storage.h>

#define AL_STORAGE_DEBUG false

static wl_handle_t al_storage_wl_handle;
static al_storage_eject_t al_storage_eject = NULL;

static tusb_desc_device_t al_storage_usb_dev_desc = {
    .bLength = sizeof(al_storage_usb_dev_desc),
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

static void al_storage_usb_msc_cb(tinyusb_msc_event_t *event) {
  // log event
  if (AL_STORAGE_DEBUG) {
    naos_log("dat: MSC event=%d mounted=%d", event->type, event->mount_changed_data.is_mounted);
  }

  // dispatch eject event on device-side re-mount
  if (event->type == TINYUSB_MSC_EVENT_MOUNT_CHANGED && event->mount_changed_data.is_mounted) {
    if (al_storage_eject != NULL) {
      al_storage_eject();
    }
  }
}

static bool al_storage_access() {
  // open file
  FILE *file = fopen(AL_STORAGE_ROOT "/TEST", "w");
  if (file == NULL) {
    return false;
  }

  // close file
  fclose(file);

  // remove file
  int ret = remove(AL_STORAGE_ROOT "/TEST");
  if (ret != 0) {
    return false;
  }

  return true;
}

void al_storage_init() {
  // mount FAT file system
  const esp_vfs_fat_mount_config_t mount_config = {
      .max_files = 2,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
  };
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_mount_rw_wl(AL_STORAGE_ROOT, "storage", &mount_config, &al_storage_wl_handle));

  // check access
  if (!al_storage_access()) {
    naos_log("al-sto: no access, formatting storage...");
    ESP_ERROR_CHECK(esp_vfs_fat_spiflash_format_rw_wl(AL_STORAGE_ROOT, "storage"));
    naos_log("al-sto: storage formatted!");
  }
}

al_storage_info_t al_storage_info() {
  // get free FATFS clusters
  FATFS *fs;
  uint32_t free_clusters;
  FRESULT res = f_getfree(AL_STORAGE_ROOT, &free_clusters, &fs);
  if (res != FR_OK) {
    // ESP_ERROR_CHECK(res);
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
  // unmount storage
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_unmount_rw_wl(AL_STORAGE_ROOT, al_storage_wl_handle));

  // find partition
  const esp_partition_t *data_partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
  if (data_partition == NULL) {
    ESP_ERROR_CHECK(ESP_ERR_NOT_FOUND);
  }

  // initialize wear levelling
  ESP_ERROR_CHECK(wl_mount(data_partition, &al_storage_wl_handle));

  // initialize USB mass storage
  const tinyusb_msc_spiflash_config_t config_spi = {
      .wl_handle = al_storage_wl_handle,
      .callback_mount_changed = al_storage_usb_msc_cb,
      .callback_premount_changed = al_storage_usb_msc_cb,
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
      .device_descriptor = &al_storage_usb_dev_desc,
      .string_descriptor = al_storage_usb_str_desc,
      .string_descriptor_count = sizeof(al_storage_usb_str_desc) / sizeof(al_storage_usb_str_desc[0]),
      .configuration_descriptor = al_storage_usb_cfg_desc,
      .self_powered = true,
      .vbus_monitor_io = GPIO_NUM_18,
  };
  ESP_ERROR_CHECK(tinyusb_driver_install(&usb_cfg));
}

void al_storage_disable_usb() {
  // uninstall driver
  ESP_ERROR_CHECK(tinyusb_driver_uninstall());

  // de-initialize USB mass storage
  tinyusb_msc_storage_deinit();

  // unmount wear levelling
  ESP_ERROR_CHECK(wl_unmount(al_storage_wl_handle));

  // remount storage
  const esp_vfs_fat_mount_config_t mount_config = {
      .max_files = 2,
      .format_if_mount_failed = true,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
  };
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_mount_rw_wl(AL_STORAGE_ROOT, "storage", &mount_config, &al_storage_wl_handle));
}

void al_storage_reset() {
  // format storage
  ESP_ERROR_CHECK(esp_vfs_fat_spiflash_format_rw_wl(AL_STORAGE_ROOT, "storage"));
}
