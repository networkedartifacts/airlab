#include <naos.h>
#include <naos/cpu.h>

#include <al/core.h>

#include "dev.h"
#include "sig.h"
#include "hmi.h"
#include "gfx.h"
#include "dat.h"
#include "rec.h"
#include "scr.h"

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

  // check storage
  dat_info_t info = dat_info();
  naos_log("main: space total=%lu free=%lu usage=%.1f%%", info.total, info.free, info.usage * 100.f);

  // run screen
  scr_run();
}

static naos_config_t config = {
    .device_type = "airlab",
    .device_version = DEV_VERSION,
    .setup_callback = setup,
};

void app_main() {
  // run naos
  naos_init(&config);
  naos_cpu_init();
  naos_start();
}
