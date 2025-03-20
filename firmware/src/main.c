#include <naos.h>
#include <naos/ble.h>
#include <naos/wifi.h>
#include <naos/mqtt.h>
#include <naos/cpu.h>
#include <naos/sys.h>

#include <al/core.h>
#include <al/sensor.h>

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

static void network() {
  // wait some time
  naos_delay(5000);

  // run network
  naos_ble_init((naos_ble_config_t){});
  naos_wifi_init();
  naos_mqtt_init(1);

  for (;;) {
    // await sample
    al_sample_t sample = al_sensor_next();

    // check connection
    if (naos_status() != NAOS_NETWORKED) {
      continue;
    }

    // publish sample
    naos_publish_d("co2", al_sample_read(sample, AL_SAMPLE_CO2), 0, false, NAOS_LOCAL);
    naos_publish_d("tmp", al_sample_read(sample, AL_SAMPLE_TMP), 0, false, NAOS_LOCAL);
    naos_publish_d("hum", al_sample_read(sample, AL_SAMPLE_HUM), 0, false, NAOS_LOCAL);
    naos_publish_d("voc", al_sample_read(sample, AL_SAMPLE_VOC), 0, false, NAOS_LOCAL);
    naos_publish_d("nox", al_sample_read(sample, AL_SAMPLE_NOX), 0, false, NAOS_LOCAL);
    naos_publish_d("prs", al_sample_read(sample, AL_SAMPLE_PRS), 0, false, NAOS_LOCAL);
  }
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

  // run network
  naos_run("net", 4096, 1, network);
}
