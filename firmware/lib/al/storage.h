#ifndef AL_STORAGE_H
#define AL_STORAGE_H

#include <stdint.h>

/**
 * The storage root directory.
 */
#define AL_STORAGE_ROOT "/fs"
#define AL_STORAGE_INTERNAL "/fs/int"
#define AL_STORAGE_EXTERNAL "/fs/ext"

/**
 * The available storage types.
 */
typedef enum {
  AL_STORAGE_INT,
  AL_STORAGE_EXT,
} al_storage_type_t;

/**
 * The storage information.
 */
typedef struct {
  uint32_t total;
  uint32_t free;
  float usage;
} al_storage_info_t;

/**
 * A storage eject callback.
 */
typedef void (*al_storage_eject_t)();

/**
 * Returns the storage information.
 *
 * @return The storage information.
 */
al_storage_info_t al_storage_info(al_storage_type_t type);

/**
 * Enable USB storage access on the external storage.
 *
 * @param eject The eject callback.
 */
void al_storage_enable_usb(al_storage_eject_t eject);

/**
 * Disables USB storage mode access on the external storage.
 */
void al_storage_disable_usb();

/**
 * Reformats the partition and resets the filesystem.
 */
void al_storage_reset();

/**
 * Read from a file in storage.
 *
 * @param dir The directory.
 * @param name The file name.
 * @param buf The buffer to read into.
 * @param offset The offset to start reading from.
 * @param length The number of bytes to read.
 * @return True if the file was read successfully, false if the file does not exist.
 */
bool al_storage_read(const char *dir, const char *name, void *buf, size_t offset, size_t length);

/**
 * Write to a file in storage.
 *
 * @param dir The directory.
 * @param name The file name.
 * @param buf The buffer to write from.
 * @param offset The offset to start writing to.
 * @param length The number of bytes to write.
 * @param truncate Whether to truncate the file before writing.
 */
void al_storage_write(const char *dir, const char *name, void *buf, size_t offset, size_t length, bool truncate);

/**
 * Deletes a file from storage.
 *
 * @param dir The directory.
 * @param name The file name.
 */
void al_storage_delete(const char *dir, const char *name);

#endif  // AL_STORAGE_H
