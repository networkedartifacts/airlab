#include <naos.h>
#include <naos/cpu.h>
#include <naos/serial.h>
#include <naos/sys.h>
#include <esp_heap_caps.h>

#include <al/core.h>
#include <al/power.h>
#include <al/store.h>
#include <al/storage.h>
#include <al/sensor.h>

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

static void sync() {
  // update storage metric
  naos_set_d("int-storage", al_storage_info(AL_STORAGE_INT).usage);
  naos_set_d("ext-storage", al_storage_info(AL_STORAGE_EXT).usage);

  // configure interval
  if (naos_get_l("long-interval") != al_store_get_interval()) {
    al_store_set_interval(naos_get_l("long-interval"));
  }
}

static void wake() {
  // set fast sensor rate
  al_sensor_set_rate(AL_SENSOR_RATE_5S);
}

static void setup() {
  // init core
  al_trigger_t trigger = al_init();

  // determine reset
  bool reset = trigger == AL_RESET;

  // initialize
  sig_init();
  hmi_init();
  gfx_init(reset);
  dat_init();
  rec_init(reset);
  com_init();

  // allow allocations in external memory
  heap_caps_malloc_extmem_enable(4096);

  // run sync
  naos_repeat("sync", 1000, sync);

  // defer wake
  naos_defer("wake", 5000, wake);

  // run screen
  scr_run(trigger);
}

static naos_param_t params[] = {
    {.name = "int-storage", .type = NAOS_DOUBLE, .mode = NAOS_VOLATILE | NAOS_LOCKED},
    {.name = "ext-storage", .type = NAOS_DOUBLE, .mode = NAOS_VOLATILE | NAOS_LOCKED},
    {.name = "sleep-rate", .type = NAOS_LONG, .default_l = 30},
    {.name = "record-rate", .type = NAOS_LONG, .default_l = 5},
    {.name = "long-interval", .type = NAOS_LONG, .default_l = 60},
    {.name = "language", .type = NAOS_STRING, .default_s = "en"},
    {.name = "fahrenheit", .type = NAOS_BOOL, .default_b = false},
    {.name = "developer", .type = NAOS_BOOL, .default_b = false},
};

static naos_config_t config = {
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
  naos_serial_init_stdio();
  naos_serial_init_secio();
  naos_start();

  // derive device name from ID if not set
  if (strlen(naos_get_s("device-name")) == 0) {
    char name[9] = {'A', 'L', 0};
    const char* id = naos_get_s("device-id");
    memcpy(name + 2, id + (strlen(id) - 6), 6);
    naos_set_s("device-name", name);
  }
}
