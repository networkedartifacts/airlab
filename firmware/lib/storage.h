#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <diskio.h>
#include <ff.h>
#include <esp_err.h>

#include <al/storage.h>

#define AL_STORAGE_INT_LABEL "internal"
#define AL_STORAGE_DEBUG false
#define AL_STORAGE_EXT_PSRAM_SIZE (2 * 1024 * 1024)
#define AL_STORAGE_EXT_PSRAM_SECTOR_SIZE 512

void al_storage_internal_init(void);
al_storage_info_t al_storage_internal_info(void);
void al_storage_internal_reset(void);

void al_storage_external_init(void);
al_storage_info_t al_storage_external_info(void);
void al_storage_external_reset(void);
void al_storage_external_mount(void);
void al_storage_external_unmount(void);
bool al_storage_external_ready(void);
uint32_t al_storage_external_block_count(void);
int32_t al_storage_external_read(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize);
int32_t al_storage_external_write(uint32_t lba, uint32_t offset, const void *buffer, uint32_t bufsize);
void al_storage_external_enable_usb(al_storage_eject_t eject);
void al_storage_external_disable_usb(void);

#endif
