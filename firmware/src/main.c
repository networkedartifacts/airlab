#include <naos.h>
#include <naos/cpu.h>
#include <naos/serial.h>
#include <naos/sys.h>
#include <naos/time.h>
#include <esp_heap_caps.h>

#include <al/clock.h>
#include <al/core.h>
#include <al/power.h>
#include <al/sample.h>
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

static void clock_cal(int32_t value) {
  // update clock calibration
  al_clock_set_calibration((int8_t)value);
}

static void long_interval(int32_t value) {
  // update store interval
  al_store_set_interval(value);
}

static void altitude(int32_t value) {
  // update pressure altitude correction
  al_sample_set_altitude((float)value);
}

static float battery() {
  // return battery level
  return al_power_get().bat_level;
}

static void wake() {
  // set fast sensor rate
  al_sensor_set_rate(AL_SENSOR_RATE_5S);
}

static void setup() {
  // init core
  al_trigger_t trigger = al_init();

  // apply clock calibration
  al_clock_set_calibration((int8_t)naos_get_l("clock-cal"));

  // apply store long interval
  al_store_set_interval(naos_get_l("long-interval"));

  // set altitude
  al_sample_set_altitude((float)naos_get_l("altitude"));

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
  heap_caps_malloc_extmem_enable(2048);

  // defer wake
  naos_defer("wake", 5000, wake);

  // run screen
  scr_run(trigger);
}

static naos_param_t params[] = {
    {.name = "int-storage", .type = NAOS_DOUBLE, .mode = NAOS_VOLATILE | NAOS_LOCKED},
    {.name = "ext-storage", .type = NAOS_DOUBLE, .mode = NAOS_VOLATILE | NAOS_LOCKED},
    {.name = "power-state", .type = NAOS_STRING, .mode = NAOS_VOLATILE | NAOS_LOCKED},
    {.name = "sleep-rate", .type = NAOS_LONG, .default_l = 30},
    {.name = "record-rate", .type = NAOS_LONG, .default_l = 5},
    {.name = "long-interval", .type = NAOS_LONG, .default_l = 60, .func_l = long_interval, .skip_func_init = true},
    {.name = "altitude", .type = NAOS_LONG, .default_l = 0, .func_l = altitude, .skip_func_init = true},
    {.name = "language", .type = NAOS_STRING, .default_s = "en"},
    {.name = "fahrenheit", .type = NAOS_BOOL, .default_b = false},
    {.name = "developer", .type = NAOS_BOOL, .default_b = true},
    {.name = "clock-cal", .type = NAOS_LONG, .func_l = clock_cal, .skip_func_init = true},  // ppm: -63..+126
};

static naos_config_t config = {
    .online_callback = com_online,
    .battery_callback = battery,
    .parameters = params,
    .num_parameters = sizeof(params) / sizeof(naos_param_t),
};

void app_main() {
  // run naos
  naos_init(&config);
  naos_cpu_init();
  naos_serial_init_secio_usj(1);
  naos_time_init();
  naos_start();

  // derive device name from ID if not set
  if (strlen(naos_get_s("device-name")) == 0) {
    char name[9] = {'A', 'L', 0};
    const char* id = naos_get_s("device-id");
    memcpy(name + 2, id + (strlen(id) - 6), 6);
    naos_set_s("device-name", name);
  }

  // run setup
  naos_run("setup", 8192, 1, setup);
}
