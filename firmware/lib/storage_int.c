#include <naos.h>
#include <esp_littlefs.h>

#include <al/storage.h>

#include "storage.h"

void al_storage_internal_init(void) {
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
}

al_storage_info_t al_storage_internal_info(void) {
  // read file system usage
  size_t total = 0;
  size_t used = 0;
  ESP_ERROR_CHECK(esp_littlefs_info(AL_STORAGE_INT_LABEL, &total, &used));

  return (al_storage_info_t){
      .total = total,
      .free = total - used,
      .usage = (float)used / (float)total,
  };
}

void al_storage_internal_reset(void) {
  // format file system
  ESP_ERROR_CHECK(esp_littlefs_format(AL_STORAGE_INT_LABEL));
}
