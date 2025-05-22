#ifndef AL_CORE_H
#define AL_CORE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * The available triggers that can wake the device.
 */
typedef enum {
  AL_RESET,
  AL_TIMEOUT,
  AL_BUTTON,
  AL_MOTION,
} al_trigger_t;

/**
 * Initializes the Air Lab.
 *
 * @return The trigger that woke the device.
 */
al_trigger_t al_init();

/**
 * Puts the device into light/deep sleep for the specified timeout or indefinitely.
 *
 * @note: Even if deep sleep is requested the device might choose to enter another
 * mode of sleep and just return from this function.
 *
 * @param deep Whether to enter deep sleep.
 * @param timeout The timeout in milliseconds or 0 for indefinite sleep.
 * @return The trigger that woke the device.
 */
al_trigger_t al_sleep(bool deep, uint64_t timeout);

#endif  // AL_CORE_H
