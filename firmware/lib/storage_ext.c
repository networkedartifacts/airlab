#include <errno.h>
#include <string.h>

#include <naos.h>
#include <diskio_impl.h>
#include <esp_heap_caps.h>
#include <esp_vfs_fat.h>

#include <al/storage.h>

#include "storage.h"

static uint8_t *al_storage_psram_disk = NULL;
static size_t al_storage_psram_disk_size = AL_STORAGE_EXT_PSRAM_SIZE;
static BYTE al_storage_psram_pdrv = FF_DRV_NOT_USED;
static FATFS *al_storage_psram_fs = NULL;

static void al_storage_psram_drive(BYTE pdrv, char *drv) {
  // format the FatFs volume prefix, as the drive number is allocated
  // dynamically and path based calls would otherwise address drive zero
  drv[0] = (char)('0' + pdrv);
  drv[1] = ':';
  drv[2] = 0;
}

static esp_err_t al_storage_psram_ensure_disk(void) {
  // keep previously allocated storage
  if (al_storage_psram_disk != NULL) {
    return ESP_OK;
  }

  // allocate PSRAM-backed block storage
  al_storage_psram_disk = heap_caps_calloc(1, al_storage_psram_disk_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (al_storage_psram_disk == NULL) {
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

static DSTATUS al_storage_psram_disk_init(BYTE pdrv) {
  return pdrv == al_storage_psram_pdrv && al_storage_psram_disk != NULL ? 0 : STA_NOINIT;
}

static DSTATUS al_storage_psram_disk_status(BYTE pdrv) { return al_storage_psram_disk_init(pdrv); }

static DRESULT al_storage_psram_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count) {
  // validate request
  if (al_storage_psram_disk_init(pdrv) != 0 || buff == NULL || count == 0) {
    return RES_PARERR;
  }

  // calculate range
  size_t offset = (size_t)sector * AL_STORAGE_EXT_PSRAM_SECTOR_SIZE;
  size_t length = (size_t)count * AL_STORAGE_EXT_PSRAM_SECTOR_SIZE;
  if (offset + length > al_storage_psram_disk_size) {
    return RES_PARERR;
  }

  // read sectors
  memcpy(buff, al_storage_psram_disk + offset, length);

  return RES_OK;
}

static DRESULT al_storage_psram_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count) {
  // validate request
  if (al_storage_psram_disk_init(pdrv) != 0 || buff == NULL || count == 0) {
    return RES_PARERR;
  }

  // calculate range
  size_t offset = (size_t)sector * AL_STORAGE_EXT_PSRAM_SECTOR_SIZE;
  size_t length = (size_t)count * AL_STORAGE_EXT_PSRAM_SECTOR_SIZE;
  if (offset + length > al_storage_psram_disk_size) {
    return RES_PARERR;
  }

  // write sectors
  memcpy(al_storage_psram_disk + offset, buff, length);

  return RES_OK;
}

static DRESULT al_storage_psram_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
  // reject uninitialized drive
  if (al_storage_psram_disk_init(pdrv) != 0) {
    return RES_NOTRDY;
  }

  // handle command
  switch (cmd) {
    case CTRL_SYNC:
      return RES_OK;
    case GET_SECTOR_COUNT:
      *(DWORD *)buff = (DWORD)(al_storage_psram_disk_size / AL_STORAGE_EXT_PSRAM_SECTOR_SIZE);
      return RES_OK;
    case GET_SECTOR_SIZE:
      *(WORD *)buff = AL_STORAGE_EXT_PSRAM_SECTOR_SIZE;
      return RES_OK;
    case GET_BLOCK_SIZE:
      *(DWORD *)buff = 1;
      return RES_OK;
    default:
      return RES_PARERR;
  }
}

static bool al_storage_access(const char *path) {
  // create test file
  FILE *file = fopen(path, "w");
  if (file == NULL) {
    naos_log("al-sto: failed to create test file, error=%d", errno);
    return false;
  }

  // close file
  fclose(file);

  // remove test file
  int ret = remove(path);
  if (ret != 0) {
    naos_log("al-sto: failed to remove test file, error=%d", ret);
    return false;
  }

  return true;
}

static size_t al_storage_fat_allocation_unit_size(size_t sector_size, size_t requested_size) {
  size_t alloc_unit_size = requested_size;
  const size_t max_size = sector_size * 128;
  if (alloc_unit_size < sector_size) {
    alloc_unit_size = sector_size;
  }
  if (alloc_unit_size > max_size) {
    alloc_unit_size = max_size;
  }
  return alloc_unit_size;
}

static esp_err_t al_storage_psram_format(const char *drv, size_t allocation_unit_size) {
  // allocate mkfs work buffer
  void *workbuf = malloc(4096);
  if (workbuf == NULL) {
    return ESP_ERR_NO_MEM;
  }

  // format file system using bounded allocation units
  size_t unit = al_storage_fat_allocation_unit_size(AL_STORAGE_EXT_PSRAM_SECTOR_SIZE, allocation_unit_size);
  const MKFS_PARM opt = {(BYTE)(FM_ANY | FM_SFD), 0, 0, 0, unit};
  FRESULT res = f_mkfs(drv, &opt, workbuf, 4096);
  free(workbuf);

  return res == FR_OK ? ESP_OK : ESP_FAIL;
}

static void al_storage_mount_external(const esp_vfs_fat_mount_config_t *mount_config) {
  // keep current mount
  if (al_storage_psram_pdrv != FF_DRV_NOT_USED) {
    return;
  }

  // ensure backing storage
  ESP_ERROR_CHECK(al_storage_psram_ensure_disk());

  // allocate FatFs drive number
  BYTE pdrv = FF_DRV_NOT_USED;
  ESP_ERROR_CHECK(ff_diskio_get_drive(&pdrv));

  // prepare drive implementation
  static const ff_diskio_impl_t impl = {
      .init = al_storage_psram_disk_init,
      .status = al_storage_psram_disk_status,
      .read = al_storage_psram_disk_read,
      .write = al_storage_psram_disk_write,
      .ioctl = al_storage_psram_disk_ioctl,
  };

  // determine drive numbers
  char drv[3];
  al_storage_psram_drive(pdrv, drv);
  al_storage_psram_pdrv = pdrv;

  // register disk implementation
  ff_diskio_register(pdrv, &impl);

  // register VFS path
  esp_err_t ret = esp_vfs_fat_register(AL_STORAGE_EXTERNAL, drv, mount_config->max_files, &al_storage_psram_fs);
  if (ret != ESP_OK) {
    al_storage_psram_pdrv = FF_DRV_NOT_USED;
    ff_diskio_unregister(pdrv);
    ESP_ERROR_CHECK(ret);
  }

  // mount file system and format if needed
  FRESULT res = f_mount(al_storage_psram_fs, drv, 1);
  if (res != FR_OK) {
    if ((res != FR_NO_FILESYSTEM && res != FR_INT_ERR) || !mount_config->format_if_mount_failed) {
      esp_vfs_fat_unregister_path(AL_STORAGE_EXTERNAL);
      al_storage_psram_pdrv = FF_DRV_NOT_USED;
      ff_diskio_unregister(pdrv);
      ESP_ERROR_CHECK(ESP_FAIL);
    }
    ESP_ERROR_CHECK(al_storage_psram_format(drv, mount_config->allocation_unit_size));
    res = f_mount(al_storage_psram_fs, drv, 0);
    if (res != FR_OK) {
      esp_vfs_fat_unregister_path(AL_STORAGE_EXTERNAL);
      al_storage_psram_pdrv = FF_DRV_NOT_USED;
      ff_diskio_unregister(pdrv);
      al_storage_psram_fs = NULL;
      ESP_ERROR_CHECK(ESP_FAIL);
    }
  }
}

void al_storage_external_init(void) {
  // mount FAT file system
  al_storage_external_mount();

  // check access
  if (!al_storage_access(AL_STORAGE_EXTERNAL "/TEST")) {
    naos_log("al-sto: no EXTERNAL access, formatting...");
    al_storage_external_unmount();
    memset(al_storage_psram_disk, 0, al_storage_psram_disk_size);
    al_storage_external_mount();
  }

  // set volume label
  char label[16];
  al_storage_psram_drive(al_storage_psram_pdrv, label);
  strcat(label, "AIRLAB");
  FRESULT res = f_setlabel(label);
  if (res != FR_OK) {
    naos_log("al-sto: failed to set label, error=%d", res);
  }
}

al_storage_info_t al_storage_external_info(void) {
  // report empty info while unmounted, as the storage is handed to USB
  if (al_storage_psram_pdrv == FF_DRV_NOT_USED) {
    return (al_storage_info_t){0};
  }

  // get free external FATFS clusters
  char drv[3];
  al_storage_psram_drive(al_storage_psram_pdrv, drv);
  FATFS *fs;
  uint32_t free_clusters;
  FRESULT res = f_getfree(drv, &free_clusters, &fs);
  if (res != FR_OK) {
    naos_log("al-sto: failed to get free clusters, error=%d", res);
    return (al_storage_info_t){0};
  }

  // calculate total and free bytes
  uint32_t total_sectors = (fs->n_fatent - 2) * fs->csize;
  uint32_t free_sectors = free_clusters * fs->csize;
  uint32_t total_bytes = total_sectors * AL_STORAGE_EXT_PSRAM_SECTOR_SIZE;
  uint32_t free_bytes = free_sectors * AL_STORAGE_EXT_PSRAM_SECTOR_SIZE;

  return (al_storage_info_t){
      .total = total_bytes,
      .free = free_bytes,
      .usage = (float)(total_bytes - free_bytes) / (float)total_bytes,
  };
}

void al_storage_external_reset(void) {
  // clear mount and storage contents
  al_storage_external_unmount();
  ESP_ERROR_CHECK(al_storage_psram_ensure_disk());
  memset(al_storage_psram_disk, 0, al_storage_psram_disk_size);

  // remount file system
  al_storage_external_mount();
}

void al_storage_external_unmount(void) {
  // ignore unmounted storage
  if (al_storage_psram_pdrv == FF_DRV_NOT_USED) {
    return;
  }

  // detach file system and VFS
  char drv[3];
  al_storage_psram_drive(al_storage_psram_pdrv, drv);
  f_mount(NULL, drv, 0);
  ESP_ERROR_CHECK(esp_vfs_fat_unregister_path(AL_STORAGE_EXTERNAL));
  ff_diskio_unregister(al_storage_psram_pdrv);

  // clear mount state
  al_storage_psram_pdrv = FF_DRV_NOT_USED;
  al_storage_psram_fs = NULL;
}

bool al_storage_external_ready(void) { return al_storage_psram_disk != NULL; }

uint32_t al_storage_external_block_count(void) {
  return (uint32_t)(al_storage_psram_disk_size / AL_STORAGE_EXT_PSRAM_SECTOR_SIZE);
}

int32_t al_storage_external_read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  // translate block access into a byte range
  size_t addr = ((size_t)lba * AL_STORAGE_EXT_PSRAM_SECTOR_SIZE) + offset;
  if (al_storage_psram_disk == NULL || addr + bufsize > al_storage_psram_disk_size) {
    return -1;
  }

  // copy data from PSRAM disk
  memcpy(buffer, al_storage_psram_disk + addr, bufsize);
  return (int32_t)bufsize;
}

int32_t al_storage_external_write(uint32_t lba, uint32_t offset, const void *buffer, uint32_t bufsize) {
  // translate block access into a byte range
  size_t addr = ((size_t)lba * AL_STORAGE_EXT_PSRAM_SECTOR_SIZE) + offset;
  if (al_storage_psram_disk == NULL || addr + bufsize > al_storage_psram_disk_size) {
    return -1;
  }

  // copy data into PSRAM disk
  memcpy(al_storage_psram_disk + addr, buffer, bufsize);
  return (int32_t)bufsize;
}

void al_storage_external_mount(void) {
  // mount FAT file system
  const esp_vfs_fat_mount_config_t mount_config = {
      .max_files = 2,
      .format_if_mount_failed = true,
      .allocation_unit_size = AL_STORAGE_EXT_PSRAM_SECTOR_SIZE,
  };
  al_storage_mount_external(&mount_config);
}
