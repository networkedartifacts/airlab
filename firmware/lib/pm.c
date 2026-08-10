#include <naos.h>
#include <naos/sys.h>

#include <bmv080.h>

#include <al/core.h>
#include <al/pm.h>

#include "internal.h"

// Chip: BMV080

#define AL_PM_TIMEOUT 1000
#define AL_PM_MAX_WORDS 512
#define AL_PM_DEBUG false

// possible I2C addresses (ABS strapping), probed in order
static const uint8_t al_pm_addrs[] = {0x54, 0x57};

static naos_mutex_t al_pm_mutex;
static bmv080_handle_t al_pm_handle = NULL;
static al_pm_state_t al_pm_state = {0};
static al_pm_hook_t al_pm_hook = NULL;

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

static void al_pm_data_ready(bmv080_output_t output, void* param) {
  // ignore callback parameter
  (void)param;

  // update state
  naos_lock(al_pm_mutex);
  al_pm_state = (al_pm_state_t){
      .valid = true,
      .pm1 = output.pm1_mass_concentration,
      .pm2_5 = output.pm2_5_mass_concentration,
      .pm10 = output.pm10_mass_concentration,
      .obstructed = output.is_obstructed,
      .out_of_range = output.is_outside_measurement_range,
  };
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

static void al_pm_task() {
  for (;;) {
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

    // serve several times per second to keep the FIFO from overflowing
    naos_delay(250);
  }
}

void al_pm_init(bool reset) {
  // create mutex
  al_pm_mutex = naos_mutex();

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

  // reset sensor
  if (reset) {
    code = bmv080_reset(al_pm_handle);
    if (code != E_BMV080_OK) {
      naos_log("al-pm: reset failed: %d", code);
    }
  }

  // select balanced measurement algorithm (trades steady-state precision for a
  // faster response, notably a quicker return to baseline)
  bmv080_measurement_algorithm_t algo = E_BMV080_MEASUREMENT_ALGORITHM_BALANCED;
  code = bmv080_set_parameter(al_pm_handle, "measurement_algorithm", &algo);
  if (code != E_BMV080_OK) {
    naos_log("al-pm: set algorithm failed: %d", code);
  }

  // start continuous measurement
  code = bmv080_start_continuous_measurement(al_pm_handle);
  if (code != E_BMV080_OK) {
    naos_log("al-pm: start failed: %d", code);
    bmv080_close(&al_pm_handle);
    al_pm_handle = NULL;
    return;
  }

  // log start
  if (AL_PM_DEBUG) {
    naos_log("al-pm: measurement started");
  }

  // serve interrupts on a dedicated task; bmv080_serve_interrupt needs a lot
  // of stack (measured ~11.5 KB peak), far more than the shared defer task
  // provides
  naos_run("al-pm", 14336, 1, al_pm_task);
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
