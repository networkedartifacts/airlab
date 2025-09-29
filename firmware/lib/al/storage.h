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
 * Get the size of a file in storage.
 *
 * @param type The storage type.
 * @param dir The directory.
 * @param name The file name.
 * @return The size of the file, or -1 if the file does not exist.
 */
int al_storage_stat(al_storage_type_t type, const char *dir, const char *name);

/**
 * Read from a file in storage.
 *
 * @param type The storage type.
 * @param dir The directory.
 * @param name The file name.
 * @param buf The buffer to read into.
 * @param offset The offset to start reading from.
 * @param length The number of bytes to read.
 * @return True if the file was read successfully, false if the file does not exist.
 */
bool al_storage_read(al_storage_type_t type, const char *dir, const char *name, void *buf, size_t offset,
                     size_t length);

/**
 * Load an entire file from storage into memory.
 *
 * @param type The storage type.
 * @param dir The directory.
 * @param name The file name.
 * @param size A pointer to store the size of the loaded file.
 * @return A pointer to the loaded file, or NULL if the file does not exist.
 */
void *al_storage_load(al_storage_type_t type, const char *dir, const char *name, size_t *size);

/**
 * Write to a file in storage.
 *
 * @param type The storage type.
 * @param dir The directory.
 * @param name The file name.
 * @param buf The buffer to write from.
 * @param offset The offset to start writing to.
 * @param length The number of bytes to write.
 * @param truncate Whether to truncate the file before writing.
 */
void al_storage_write(al_storage_type_t type, const char *dir, const char *name, void *buf, size_t offset,
                      size_t length, bool truncate);

/**
 * Deletes a file from storage.
 *
 * @param type The storage type.
 * @param dir The directory.
 * @param name The file name.
 */
void al_storage_delete(al_storage_type_t type, const char *dir, const char *name);

#endif  // AL_STORAGE_H
