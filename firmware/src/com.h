#ifndef COM_H
#define COM_H

#include <stdbool.h>
#include <stddef.h>

void com_init();

/**
 * Starts the radios. They are only started once the device is fully awake, as
 * they cannot be stopped again and a device that merely wakes up to refresh
 * the display or take a measurement must not pay for them.
 */
void com_start();

/**
 * Returns whether the radios have been started.
 */
bool com_started();

void com_online();

void com_log(const char *, size_t len);

#endif  // COM_H
