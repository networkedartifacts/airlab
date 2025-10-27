#ifndef AL_POWER_H
#define AL_POWER_H

#include <stdbool.h>

/**
 * The power state.
 */
typedef struct {
  float bat_voltage;
  float bat_level;
  bool has_usb;
  bool can_fast;
  bool charging;
} al_power_state_t;

/**
 * The power hook.
 */
typedef void (*al_power_hook_t)(al_power_state_t);

/**
 * Configure the power controller.
 *
 * @param hook The power hook.
 */
void al_power_config(al_power_hook_t hook);

/**
 * Returns the current power state.
 */
al_power_state_t al_power_get();

/**
 * Turns the device power off.
 */
void al_power_off();

/**
 * Puts the device into ship mode.
 */
void al_power_ship();

#endif  // AL_POWER_H
