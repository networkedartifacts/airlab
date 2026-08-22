#ifndef AL_STORAGE_H
#define AL_STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// host shim shadowing lib/al/storage.h: same API, but the roots point to a
// relative scratch directory (tests run from firmware/) and the functions are
// implemented over plain POSIX files by the test suite

#define AL_STORAGE_ROOT "test/fs"
#define AL_STORAGE_INTERNAL "test/fs/int"
#define AL_STORAGE_EXTERNAL "test/fs/ext"

typedef enum {
  AL_STORAGE_INT,
  AL_STORAGE_EXT,
} al_storage_type_t;

typedef struct {
  uint32_t total;
  uint32_t free;
  float usage;
} al_storage_info_t;

typedef void (*al_storage_eject_t)();

void al_storage_prepare(al_storage_type_t type);
al_storage_info_t al_storage_info(al_storage_type_t type);
void al_storage_enable_usb(al_storage_eject_t eject);
void al_storage_disable_usb();
void al_storage_reset();
int al_storage_stat(al_storage_type_t type, const char *dir, const char *name);
bool al_storage_read(al_storage_type_t type, const char *dir, const char *name, void *buf, size_t offset,
                     size_t length);
void *al_storage_load(al_storage_type_t type, const char *dir, const char *name, size_t *size);
void al_storage_write(al_storage_type_t type, const char *dir, const char *name, void *buf, size_t offset,
                      size_t length, bool truncate);
void al_storage_delete(al_storage_type_t type, const char *dir, const char *name);

#endif  // AL_STORAGE_H
