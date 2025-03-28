#include <naos.h>
#include <naos/cpu.h>
#include <naos/sys.h>

#include <al/core.h>
#include <al/power.h>

#include "dev.h"
#include "sig.h"
#include "hmi.h"
#include "gfx.h"
#include "dat.h"
#include "rec.h"
#include "com.h"
#include "scr.h"

static float battery() {
  // return battery level
  return al_power_get().battery;
}

static void storage() {
  // update storage metric
  naos_set_d("storage", dat_info().usage);
}

static void setup() {
  // init core
  al_init();

  // get trigger
  al_trigger_t trigger = al_trigger();
  naos_log("main: trigger=%d", trigger);

  // determine reset
  bool reset = trigger == AL_RESET;

  // initialize
  sig_init();
  hmi_init();
  gfx_init(reset);
  dat_init();
  rec_init();
  com_init();

  // update storage
  naos_repeat("storage", 1000, storage);

  // run screen
  scr_run();
}

static naos_param_t params[] = {
    {.name = "storage", .type = NAOS_DOUBLE},
};

static naos_config_t config = {
    .device_type = "airlab",
    .device_version = DEV_VERSION,
    .setup_callback = setup,
    .online_callback = com_online,
    .battery_callback = battery,
    .parameters = params,
    .num_parameters = sizeof(params) / sizeof(naos_param_t),
};

void app_main() {
  // run naos
  naos_init(&config);
  naos_cpu_init();
  naos_start();
}
