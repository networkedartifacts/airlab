#ifndef AL_POWER_H
#define AL_POWER_H

#include <stdbool.h>

/**
 * The charge phase reported by the PMIC.
 */
typedef enum {
  AL_POWER_PHASE_NONE = 0,
  AL_POWER_PHASE_PRE = 1,
  AL_POWER_PHASE_FAST = 2,
  AL_POWER_PHASE_TERM = 3,
  AL_POWER_PHASE_USB = 4,
} al_power_phase_t;

/**
 * The power state.
 */
typedef struct {
  float bat_voltage;
  float bat_level;
  bool bat_low;
  bool has_usb;
  bool can_fast;
  bool charging;
  bool hiz;
  al_power_phase_t phase;
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
 * Sets the charger input high-impedance mode. While enabled, the device runs
 * from the battery even if USB power is present. The charger watchdog is
 * disabled while active to keep the setting across sleep phases.
 *
 * @param enable Whether hi-Z mode is enabled.
 */
void al_power_hiz(bool enable);

/**
 * Turns the device power off.
 */
void al_power_off();

/**
 * Puts the device into ship mode.
 */
void al_power_ship();

#endif  // AL_POWER_H
