#include <naos.h>
#include <naos/sys.h>

#include <bmv080.h>

#include <al/core.h>
#include <al/clock.h>

#include "internal.h"
#include "sensor_pm.h"

// Chip: BMV080

#define AL_PM_TIMEOUT 1000
#define AL_PM_MAX_WORDS 512
#define AL_PM_DEBUG false

// duration of a burst measurement (ms), matching the sensors integration time
#define AL_PM_BURST_TIME 10000

// retry delay after a failed measurement start (ms)
#define AL_PM_RETRY_DELAY 60000

// possible I2C addresses (ABS strapping), probed in order
static const uint8_t al_pm_addrs[] = {0x54, 0x57};

static naos_mutex_t al_pm_mutex;
static naos_signal_t al_pm_signal;
static bmv080_handle_t al_pm_handle = NULL;
static al_pm_state_t al_pm_state = {0};
static al_pm_hook_t al_pm_hook = NULL;
static al_pm_mode_t al_pm_want_mode = AL_PM_IDLE;
static int32_t al_pm_want_period = 0;
static int32_t al_pm_want_ttl = 0;
static al_pm_mode_t al_pm_mode = AL_PM_IDLE;
static int32_t al_pm_period = 0;
static int32_t al_pm_ttl = 0;  // cache lifetime for new readings (s)
static int32_t al_pm_burst_ttl = 0;
static bool al_pm_suspended = false;
static bool al_pm_established = false;
static bool al_pm_settle = false;
static int64_t al_pm_retry_after = 0;

// last unobstructed reading, kept across deep sleep
AL_KEEP static int16_t al_pm_cache_value = -1;
AL_KEEP static int64_t al_pm_cache_time = 0;
AL_KEEP static int32_t al_pm_cache_ttl = 0;

// whether the sensor was left idle, kept across deep sleep
AL_KEEP static bool al_pm_clean = true;

static int8_t al_pm_read(bmv080_sercom_handle_t sercom, uint16_t header, uint16_t* payload, uint16_t length) {
  // decode address
  uint8_t addr = (uint8_t)(uintptr_t)sercom;

  // guard payload length
  if (length > AL_PM_MAX_WORDS) {
    naos_log("al-pm: read payload too large: %u", length);
    return -1;
  }

  // shift header into the I2C register address
  uint16_t reg = (uint16_t)(header << 1);

  // assemble register address (MSB first)
  uint8_t hdr[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xff)};

  // write register address (terminated with a stop)
  esp_err_t err = al_i2c_transfer(addr, hdr, 2, NULL, 0, AL_PM_TIMEOUT);
  if (err != ESP_OK) {
    return -1;
  }

  // read payload bytes in a separate transaction
  static uint8_t buf[AL_PM_MAX_WORDS * 2];
  err = al_i2c_transfer(addr, NULL, 0, buf, (size_t)length * 2, AL_PM_TIMEOUT);
  if (err != ESP_OK) {
    return -1;
  }

  // combine bytes into 16 bit words (MSB first)
  for (uint16_t i = 0; i < length; i++) {
    payload[i] = ((uint16_t)buf[2 * i] << 8) | (uint16_t)buf[2 * i + 1];
  }

  return 0;
}

static int8_t al_pm_write(bmv080_sercom_handle_t sercom, uint16_t header, const uint16_t* payload, uint16_t length) {
  // decode address
  uint8_t addr = (uint8_t)(uintptr_t)sercom;

  // guard payload length
  if (length > AL_PM_MAX_WORDS) {
    naos_log("al-pm: write payload too large: %u", length);
    return -1;
  }

  // shift header into the I2C register address
  uint16_t reg = (uint16_t)(header << 1);

  // assemble register address and payload (MSB first)
  static uint8_t buf[2 + AL_PM_MAX_WORDS * 2];
  buf[0] = (uint8_t)(reg >> 8);
  buf[1] = (uint8_t)(reg & 0xff);
  for (uint16_t i = 0; i < length; i++) {
    buf[2 + 2 * i] = (uint8_t)(payload[i] >> 8);
    buf[2 + 2 * i + 1] = (uint8_t)(payload[i] & 0xff);
  }

  // write header and payload in one transaction
  esp_err_t err = al_i2c_transfer(addr, buf, (size_t)2 + (size_t)length * 2, NULL, 0, AL_PM_TIMEOUT);
  if (err != ESP_OK) {
    return -1;
  }

  return 0;
}

static int8_t al_pm_delay(uint32_t ms) {
  // delay execution
  naos_delay(ms);

  return 0;
}

static uint32_t al_pm_tick() {
  // return milliseconds since boot
  return (uint32_t)naos_millis();
}

static void al_pm_data_ready(bmv080_output_t output, void* param) {
  // ignore callback parameter
  (void)param;

  // update state and cache
  naos_lock(al_pm_mutex);
  al_pm_state = (al_pm_state_t){
      .valid = true,
      .pm1 = output.pm1_mass_concentration,
      .pm2_5 = output.pm2_5_mass_concentration,
      .pm10 = output.pm10_mass_concentration,
      .obstructed = output.is_obstructed,
      .out_of_range = output.is_outside_measurement_range,
  };
  if (!output.is_obstructed) {
    float value = output.pm2_5_mass_concentration * 10.f;
    if (value < 0.f) {
      value = 0.f;
    } else if (value > 30000.f) {
      value = 30000.f;
    }
    al_pm_cache_value = (int16_t)value;
    al_pm_cache_time = al_clock_get_epoch();
    al_pm_cache_ttl = al_pm_ttl;
  }
  al_pm_established = true;
  al_pm_state_t state = al_pm_state;
  al_pm_hook_t hook = al_pm_hook;
  naos_unlock(al_pm_mutex);

  // log data
  if (AL_PM_DEBUG) {
    naos_log("al-pm: pm1=%.1f pm2.5=%.1f pm10=%.1f obstructed=%d range=%d", state.pm1, state.pm2_5, state.pm10,
             state.obstructed, state.out_of_range);
  }

  // dispatch state
  if (hook != NULL) {
    hook(state);
  }
}

static void al_pm_serve() {
  // serve pending interrupt (dispatches al_pm_data_ready per available sample)
  bmv080_status_code_t code = bmv080_serve_interrupt(al_pm_handle, al_pm_data_ready, NULL);
  if (code != E_BMV080_OK) {
    // codes 1-4 and 208-209 are warnings (e.g. FIFO overflow under load)
    bool warning = code <= E_BMV080_WARNING_FIFO_EVENTS_OVERFLOW || code == E_BMV080_WARNING_FIFO_SW_BUFFER_SIZE ||
                   code == E_BMV080_WARNING_FIFO_HW_BUFFER_SIZE;
    if (!warning) {
      naos_log("al-pm: serve failed: %d", code);
    } else if (AL_PM_DEBUG) {
      naos_log("al-pm: serve warning: %d", code);
    }
  }
}

static void al_pm_start(al_pm_mode_t mode, int32_t period, int32_t ttl) {
  // set cache lifetime for upcoming readings
  naos_lock(al_pm_mutex);
  al_pm_ttl = ttl;
  naos_unlock(al_pm_mutex);

  // start measurement in the requested mode
  bmv080_status_code_t code;
  if (mode == AL_PM_CYCLED) {
    uint16_t value = (uint16_t)period;
    code = bmv080_set_parameter(al_pm_handle, "duty_cycling_period", &value);
    if (code != E_BMV080_OK) {
      naos_log("al-pm: set period failed: %d", code);
    }
    code = bmv080_start_duty_cycling_measurement(al_pm_handle, al_pm_tick, E_BMV080_DUTY_CYCLING_MODE_0);
  } else {
    code = bmv080_start_continuous_measurement(al_pm_handle);
  }
  if (code != E_BMV080_OK) {
    naos_log("al-pm: start failed: %d", code);
    al_pm_retry_after = naos_millis() + AL_PM_RETRY_DELAY;
    return;
  }

  // update mode
  naos_lock(al_pm_mutex);
  al_pm_mode = mode;
  al_pm_period = period;
  al_pm_clean = false;
  al_pm_established = false;
  naos_unlock(al_pm_mutex);
}

static void al_pm_stop() {
  // await an established measurement (first reading arrived), as stopping
  // during the start sequence leaves the sensor in its active state
  naos_lock(al_pm_mutex);
  bool established = al_pm_established;
  naos_unlock(al_pm_mutex);
  int64_t deadline = naos_millis() + 5000;
  while (!established && naos_millis() < deadline) {
    bmv080_serve_interrupt(al_pm_handle, al_pm_data_ready, NULL);
    naos_delay(100);
    naos_lock(al_pm_mutex);
    established = al_pm_established;
    naos_unlock(al_pm_mutex);
  }

  // stop measurement
  bmv080_status_code_t code = bmv080_stop_measurement(al_pm_handle);
  if (code != E_BMV080_OK) {
    naos_log("al-pm: stop failed: %d", code);
  }

  // update mode
  naos_lock(al_pm_mutex);
  al_pm_mode = AL_PM_IDLE;
  al_pm_clean = code == E_BMV080_OK && established;
  naos_unlock(al_pm_mutex);
}

static void al_pm_task() {
  // reset and settle the sensor if required: the reset leaves the sensor in
  // its active boot state, and only stopping an established measurement
  // enters the low-power sleep mode
  if (al_pm_settle) {
    bmv080_status_code_t code = bmv080_reset(al_pm_handle);
    if (code != E_BMV080_OK) {
      naos_log("al-pm: reset failed: %d", code);
    }
    code = bmv080_start_continuous_measurement(al_pm_handle);
    if (code == E_BMV080_OK) {
      int64_t deadline = naos_millis() + 5000;
      while (naos_millis() < deadline && !al_pm_get().valid) {
        bmv080_serve_interrupt(al_pm_handle, al_pm_data_ready, NULL);
        naos_delay(100);
      }
      code = bmv080_stop_measurement(al_pm_handle);
    }
    if (code != E_BMV080_OK) {
      naos_log("al-pm: settle failed: %d", code);
    }
    naos_lock(al_pm_mutex);
    al_pm_clean = true;
    al_pm_settle = false;
    naos_unlock(al_pm_mutex);
  }

  // select balanced measurement algorithm (trades steady-state precision for
  // a faster response, notably a quicker return to baseline), a potential
  // reset reverted it to the default
  bmv080_measurement_algorithm_t algo = E_BMV080_MEASUREMENT_ALGORITHM_BALANCED;
  bmv080_status_code_t code = bmv080_set_parameter(al_pm_handle, "measurement_algorithm", &algo);
  if (code != E_BMV080_OK) {
    naos_log("al-pm: set algorithm failed: %d", code);
  }

  for (;;) {
    // capture control state
    naos_lock(al_pm_mutex);
    bool suspended = al_pm_suspended;
    al_pm_mode_t want = suspended ? AL_PM_IDLE : al_pm_want_mode;
    int32_t period = al_pm_want_period;
    int32_t ttl = al_pm_want_ttl;
    int32_t burst_ttl = suspended ? 0 : al_pm_burst_ttl;
    if (suspended) {
      al_pm_burst_ttl = 0;
    }
    naos_unlock(al_pm_mutex);

    // reconcile measurement mode
    if ((want != al_pm_mode || (want == AL_PM_CYCLED && period != al_pm_period)) &&
        naos_millis() >= al_pm_retry_after) {
      if (al_pm_mode != AL_PM_IDLE) {
        al_pm_stop();
      }
      if (want != AL_PM_IDLE) {
        al_pm_start(want, period, ttl);
      }
    }

    // serve a running measurement, or execute a requested burst: a continuous
    // measurement over the sensors integration time to refresh the cache
    if (al_pm_mode != AL_PM_IDLE) {
      al_pm_serve();
    } else if (burst_ttl > 0) {
      al_pm_start(AL_PM_CONTINUOUS, 0, burst_ttl);
      if (al_pm_mode != AL_PM_IDLE) {
        int64_t deadline = naos_millis() + AL_PM_BURST_TIME;
        while (naos_millis() < deadline) {
          al_pm_serve();
          naos_delay(250);
          naos_lock(al_pm_mutex);
          bool abort = al_pm_suspended;
          naos_unlock(al_pm_mutex);
          if (abort) {
            break;
          }
        }
        al_pm_stop();
      }
      naos_lock(al_pm_mutex);
      al_pm_burst_ttl = 0;
      naos_unlock(al_pm_mutex);
    }

    // await control changes or pace servings
    naos_await(al_pm_signal, 1, true, al_pm_mode != AL_PM_IDLE ? 250 : 1000);
  }
}

void al_pm_init(bool reset) {
  // create mutex and signal
  al_pm_mutex = naos_mutex();
  al_pm_signal = naos_signal();

  // clear cache on reset
  if (reset) {
    al_pm_cache_value = -1;
    al_pm_cache_time = 0;
    al_pm_cache_ttl = 0;
  }

  // probe possible addresses to detect the sensor, as the SDK otherwise fails
  // opaquely and with unbounded cost on boards without the sensor
  uint8_t addr = 0;
  for (size_t i = 0; i < sizeof(al_pm_addrs); i++) {
    uint8_t byte = 0;
    if (al_i2c_transfer(al_pm_addrs[i], NULL, 0, &byte, 1, AL_PM_TIMEOUT) == ESP_OK) {
      addr = al_pm_addrs[i];
      break;
    }
  }
  if (addr == 0) {
    naos_log("al-pm: chip=none");
    return;
  }

  // open sensor (address is carried as the opaque sercom handle)
  bmv080_sercom_handle_t sercom = (bmv080_sercom_handle_t)(uintptr_t)addr;
  bmv080_status_code_t code = bmv080_open(&al_pm_handle, sercom, al_pm_read, al_pm_write, al_pm_delay);
  if (code != E_BMV080_OK) {
    naos_log("al-pm: open failed: %d", code);
    al_pm_handle = NULL;
    return;
  }

  // log identification with driver version and sensor id
  uint16_t major = 0, minor = 0, patch = 0;
  char git_hash[13] = {0};
  int32_t commits = 0;
  bmv080_get_driver_version(&major, &minor, &patch, git_hash, &commits);
  char id[13] = {0};
  bmv080_get_sensor_id(al_pm_handle, id);
  naos_log("al-pm: chip=BMV080 driver=v%u.%u.%u id=%s", major, minor, patch, id);

  // require a reset and settle if requested, or if a measurement may still
  // be running from before a reboot: the SDK tracks the measurement state per
  // handle and cannot stop measurements started by a previous boot
  al_pm_settle = reset || !al_pm_clean;
  if (!reset && !al_pm_clean) {
    naos_log("al-pm: forcing reset");
  }

  // operate sensor on a dedicated task; bmv080_serve_interrupt needs a lot
  // of stack (measured ~11.5 KB peak), far more than the shared defer task
  // provides
  naos_run("al-pm", 14336, 1, al_pm_task);
}

bool al_pm_present() {
  // return chip presence
  return al_pm_handle != NULL;
}

void al_pm_run(al_pm_mode_t mode, int32_t period, int32_t ttl) {
  // skip if no chip was detected
  if (al_pm_handle == NULL) {
    return;
  }

  // request mode
  naos_lock(al_pm_mutex);
  al_pm_want_mode = mode;
  al_pm_want_period = period;
  al_pm_want_ttl = ttl;
  naos_unlock(al_pm_mutex);
  naos_trigger(al_pm_signal, 1, false);
}

void al_pm_burst(int32_t ttl) {
  // skip if no chip was detected
  if (al_pm_handle == NULL) {
    return;
  }

  // request burst
  naos_lock(al_pm_mutex);
  al_pm_burst_ttl = ttl;
  naos_unlock(al_pm_mutex);
  naos_trigger(al_pm_signal, 1, false);
}

void al_pm_flush() {
  // skip if no chip was detected
  if (al_pm_handle == NULL) {
    return;
  }

  // wait until the requested mode is applied and requested bursts are done,
  // bounded in case the sensor persistently fails
  int64_t deadline = naos_millis() + AL_PM_BURST_TIME + 5000;
  while (naos_millis() < deadline) {
    naos_lock(al_pm_mutex);
    al_pm_mode_t want = al_pm_suspended ? AL_PM_IDLE : al_pm_want_mode;
    bool settled = !al_pm_settle && al_pm_burst_ttl == 0 && al_pm_mode == want;
    naos_unlock(al_pm_mutex);
    if (settled) {
      return;
    }
    naos_delay(100);
  }
}

int32_t al_pm_age() {
  // return seconds since the last cached reading
  naos_lock(al_pm_mutex);
  int64_t time = al_pm_cache_time;
  naos_unlock(al_pm_mutex);
  if (al_pm_handle == NULL || time == 0) {
    return INT32_MAX;
  }
  int64_t age = (al_clock_get_epoch() - time) / 1000;
  if (age < 0) {
    age = 0;
  } else if (age > INT32_MAX) {
    age = INT32_MAX;
  }

  return (int32_t)age;
}

int16_t al_pm_sample(int64_t epoch) {
  // return the cached value if the epoch falls within its lifetime
  naos_lock(al_pm_mutex);
  int16_t value = -1;
  if (al_pm_cache_time > 0 && al_pm_cache_ttl > 0) {
    int64_t diff = epoch - al_pm_cache_time;
    if (diff < 0) {
      diff = -diff;
    }
    if (diff <= (int64_t)al_pm_cache_ttl * 1000) {
      value = al_pm_cache_value;
    }
  }
  naos_unlock(al_pm_mutex);

  return value;
}

void al_pm_sleep() {
  // skip if no chip was detected
  if (al_pm_handle == NULL) {
    return;
  }

  // suspend operation, aborting a running burst
  naos_lock(al_pm_mutex);
  al_pm_suspended = true;
  naos_unlock(al_pm_mutex);
  naos_trigger(al_pm_signal, 1, false);

  // await an idle sensor, bounded in case the sensor persistently fails
  int64_t deadline = naos_millis() + 10000;
  while (naos_millis() < deadline) {
    naos_lock(al_pm_mutex);
    bool idle = !al_pm_settle && al_pm_mode == AL_PM_IDLE && al_pm_burst_ttl == 0;
    naos_unlock(al_pm_mutex);
    if (idle) {
      return;
    }
    naos_delay(50);
  }
  naos_log("al-pm: sleep timeout");
}

void al_pm_wake() {
  // skip if no chip was detected
  if (al_pm_handle == NULL) {
    return;
  }

  // resume operation
  naos_lock(al_pm_mutex);
  al_pm_suspended = false;
  naos_unlock(al_pm_mutex);
  naos_trigger(al_pm_signal, 1, false);
}

void al_pm_config(al_pm_hook_t hook) {
  // store hook
  naos_lock(al_pm_mutex);
  al_pm_hook = hook;
  naos_unlock(al_pm_mutex);
}

al_pm_state_t al_pm_get() {
  // capture state
  naos_lock(al_pm_mutex);
  al_pm_state_t state = al_pm_state;
  naos_unlock(al_pm_mutex);

  return state;
}
