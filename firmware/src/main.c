#include <naos.h>

#include "dev.h"
#include "sig.h"
#include "rtc.h"
#include "acc.h"
#include "cap.h"
#include "pwr.h"
#include "btn.h"
#include "epd.h"
#include "gfx.h"
#include "sns.h"
#include "dat.h"
#include "rec.h"
#include "scr.h"
#include "sys.h"

static void setup() {
  // log
  naos_log("setup");

  // initialize
  dev_init();
  sig_init();
  pwr_init();
  btn_init();
  acc_init();
  cap_init();
  epd_init();
  gfx_init();
  sns_init();
  dat_init();
  rec_init();

  // check storage
  dat_info_t info = dat_info();
  naos_log("main: space total=%lu free=%lu usage=%.1f%%", info.total, info.free, info.usage * 100.f);

  // sync time
  rtc_state_t state = rtc_get();
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
  naos_start();
}
