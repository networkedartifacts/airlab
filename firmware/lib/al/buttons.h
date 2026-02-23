#ifndef AL_BUTTONS_H
#define AL_BUTTONS_H

#include <stdint.h>

#define AL_BUTTON_ENTER 0
#define AL_BUTTON_ESCAPE 1
#define AL_BUTTON_UP 2
#define AL_BUTTON_RIGHT 3
#define AL_BUTTON_DOWN 4
#define AL_BUTTON_LEFT 5

/**
 * Returns the current button state as a bitfield.
 */
uint8_t al_buttons_get();

/**
 * Returns the button that caused the deep sleep wakeup as a bitfield.
 */
uint8_t al_buttons_wakeup();

#endif  // AL_BUTTONS_H
