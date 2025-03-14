#include <naos.h>
#include <naos/cpu.h>

#include <al/core.h>
#include <al/clock.h>

#include "dev.h"
#include "sig.h"
#include "cap.h"
#include "btn.h"
#include "gfx.h"
#include "sns.h"
#include "dat.h"
#include "rec.h"
#include "scr.h"
#include "sys.h"

static void setup() {
  // log
  naos_log("setup");

  // init core
  al_init();

  // initialize
  sig_init();
  btn_init();
  cap_init();
  gfx_init();
  sns_init();
  dat_init();
  rec_init();

  // check storage
  dat_info_t info = dat_info();
  naos_log("main: space total=%lu free=%lu usage=%.1f%%", info.total, info.free, info.usage * 100.f);

  // sync time
  al_clock_state_t state = al_clock_get();
  sys_set_date(state.year, state.month, state.day);
  sys_set_time(state.hours, state.minutes, state.seconds);
  naos_log("main: time %02d-%02d-%02d %02d:%02d:%02d", state.year, state.month, state.day, state.hours, state.minutes,
           state.seconds);

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
