#include <naos.h>
#include <naos/sys.h>

#include <al/accel.h>

#include "internal.h"
#include "accel.h"

typedef enum {
  AL_ACCEL_NONE,
  AL_ACCEL_FXL,
  AL_ACCEL_LIS,
} al_accel_chip_t;

static al_accel_chip_t al_accel_chip = AL_ACCEL_NONE;
static naos_mutex_t al_accel_mutex;
static al_accel_state_t al_accel_state = {0};
static al_accel_hook_t al_accel_hook = NULL;

void al_accel_init(bool reset) {
  // detect chip
  const char *chip = "none";
  if (al_accel_lis_detect()) {
    al_accel_chip = AL_ACCEL_LIS;
    chip = "LIS2DH12";
  } else if (al_accel_fxl_detect()) {
    al_accel_chip = AL_ACCEL_FXL;
    chip = "FXLS8974CF";
  }
  naos_log("al-acc: chip=%s", chip);

  // initialize chip; quiesce a legacy chip mounted alongside the new one, so
  // it releases the shared interrupt line
  if (al_accel_chip == AL_ACCEL_LIS) {
    if (al_accel_fxl_detect()) {
      al_accel_fxl_quiesce();
    }
    al_accel_lis_init(reset);
  } else if (al_accel_chip == AL_ACCEL_FXL) {
    al_accel_fxl_init(reset);
  }

  // create mutex
  al_accel_mutex = naos_mutex();

  // check immediately to clear interrupt
  al_accel_check();

  // run check task to ensure the interrupt is cleared eventually
  naos_repeat("al-acc", 1000, al_accel_check);
}

void al_accel_check() {
  // skip if no chip was detected
  if (al_accel_chip == AL_ACCEL_NONE) {
    return;
  }

  // lock mutex
  naos_lock(al_accel_mutex);

  // update state, starting from the previous state
  al_accel_state_t state = al_accel_state;
  bool ok;
  if (al_accel_chip == AL_ACCEL_LIS) {
    ok = al_accel_lis_check(&state);
  } else {
    ok = al_accel_fxl_check(&state);
  }
  if (!ok) {
    naos_unlock(al_accel_mutex);
    return;
  }

  // determine if state changed
  bool changed = state.front != al_accel_state.front || state.rotation != al_accel_state.rotation;

  // update state
  al_accel_state = state;

  // unlock mutex
  naos_unlock(al_accel_mutex);

  // dispatch state if changed
  if (changed && al_accel_hook != NULL) {
    al_accel_hook(state);
  }
}

void al_accel_config(al_accel_hook_t hook) {
  // store hook
  naos_lock(al_accel_mutex);
  al_accel_hook = hook;
  naos_unlock(al_accel_mutex);
}

al_accel_state_t al_accel_get() {
  // capture state
  naos_lock(al_accel_mutex);
  al_accel_state_t state = al_accel_state;
  naos_unlock(al_accel_mutex);

  return state;
}
