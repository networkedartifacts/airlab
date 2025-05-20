#ifndef AL_STORAGE_H
#define AL_STORAGE_H

#include <stdint.h>

/**
 * The storage root directory.
 */
#define AL_STORAGE_ROOT "/fs"

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
al_storage_info_t al_storage_info();

/**
 * Enables USB storage mode.
 *
 * @param eject The eject callback.
 */
void al_storage_enable_usb(al_storage_eject_t eject);

/**
 * Disables USB storage mode.
 */
void al_storage_disable_usb();

/**
 * Reformats the partition and resets the filesystem.
 */
void al_storage_reset();

#endif  // AL_STORAGE_H
