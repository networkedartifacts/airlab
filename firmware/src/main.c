#include <naos.h>
#include <driver/gpio.h>

#include "dev.h"
#include "sig.h"
#include "rtc.h"
#include "pwr.h"
#include "btn.h"
#include "epd.h"
#include "gfx.h"
#include "sns.h"
#include "dat.h"
#include "rec.h"
#include "scr.h"

static void setup() {
  // log
  naos_log("setup");

  // hold power
  gpio_config_t cfg = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = BIT64(GPIO_NUM_21),
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));
  ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_21, 1));

  // initialize
  dev_init();
  sig_init();
  // pwr_init();
  // btn_init();
  rtc2_init();
  epd_init();
  gfx_init();
  sns_init();
  dat_init();
  rec_init();

  // check storage
  dat_info_t info = dat_info();
  naos_log("dat_info: total=%lu free=%lu usage=%.1f%%", info.total, info.free, info.usage * 100.f);

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
