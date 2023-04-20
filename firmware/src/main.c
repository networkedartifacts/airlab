#include <naos.h>

#include "dev.h"
#include "sig.h"
#include "pwr.h"
#include "btn.h"
#include "epd.h"
#include "gfx.h"
#include "sns.h"
#include "dat.h"
#include "rec.h"
#include "scr.h"

static void setup() {
  // initialize
  dev_init();
  sig_init();
  pwr_init();
  btn_init();
  epd_init();
  gfx_init();
  sns_init();
  dat_init();
  rec_init();

  // run screen
  scr_run();
}

static naos_config_t config = {
    .device_type = "airlab",
    .device_version = "0.1.0",
    .setup_callback = setup,
};

void app_main() {
  // run naos
  naos_init(&config);
  naos_start();
}
