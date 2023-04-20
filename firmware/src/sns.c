#include <naos.h>
#include <naos_sys.h>
#include <driver/i2c.h>

#include "sns.h"
#include "sig.h"

#define SNS_ADDR 0x62
#define SNS_DEBUG false

// TODO: Support low power measurement mode (30s).

static naos_mutex_t sns_mutex;
static naos_signal_t sns_signal;
static sns_state_t sns_state = {0};
static uint16_t sns_write[8];
static uint16_t sns_read[8];
static uint8_t sns_buffer[24];

uint8_t sns_crc(const uint8_t* data, uint16_t count) {
  // crc-8 calculation as defined per datasheet
  uint8_t crc = 0xFF;
  for (uint16_t byte = 0; byte < count; ++byte) {
    crc ^= (data[byte]);
    for (uint8_t bit = 8; bit > 0; --bit) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x31;
      } else {
        crc = (crc << 1);
      }
    }
  }

  return crc;
}

static void sns_transfer(uint16_t addr, size_t send, size_t receive, bool may_fail) {
  // write address
  sns_buffer[0] = addr >> 8;
  sns_buffer[1] = addr & 0xFF;

  // write bytes
  for (size_t i = 0; i < send; i++) {
    sns_buffer[2 + i * 3] = sns_write[i] >> 8;
    sns_buffer[2 + i * 3 + 1] = sns_write[i] & 0xFF;
    sns_buffer[2 + i * 3 + 2] = sns_crc(sns_buffer + (2 + i * 2), 2);
  }

  // run command
  esp_err_t err;
  if (receive > 0) {
    err = i2c_master_write_read_device(I2C_NUM_0, SNS_ADDR, sns_buffer, 2 + send * 3, sns_buffer, receive * 3, 1000);
  } else {
    err = i2c_master_write_to_device(I2C_NUM_0, SNS_ADDR, sns_buffer, 2 + send * 3, 1000);
  }
  if (!may_fail) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);
  }
  if (err != ESP_OK) {
    return;
  }

  // read bytes
  for (size_t i = 0; i < receive; i++) {
    sns_read[i] = (sns_buffer[i * 3] << 8) | sns_buffer[i * 3 + 1];
    uint8_t crc = sns_crc(sns_buffer + (i * 3), 2);
    if (sns_buffer[i * 3 + 2] != crc) {
      naos_log("snd: crc failed in=%u crc=%u", sns_buffer[i * 3 + 2], crc);
      ESP_ERROR_CHECK_WITHOUT_ABORT(ESP_FAIL);
    }
  }
}

static void sns_check() {
  for (;;) {
    // wait a second
    naos_delay(1000);

    // acquire mutex
    naos_lock(sns_mutex);

    // check if measurement is available
    sns_transfer(0xe4b8, 0, 1, false);
    if ((sns_read[0] & 0xFFF) == 0) {
      if (SNS_DEBUG) {
        naos_log("sns: measurement not ready");
      }
      naos_unlock(sns_mutex);
      continue;
    }

    // read sensor
    sns_transfer(0xec05, 0, 3, false);

    // calculate values
    uint16_t co2 = sns_read[0];                                              // ppm
    float tmp = -45.f + 175.f * ((float)sns_read[1] / (float)(UINT16_MAX));  // °C
    float hum = 100.f * ((float)sns_read[2] / (float)(UINT16_MAX));          // % rH
    if (SNS_DEBUG) {
      naos_log("sns: read measurement: co2=%ld tmp=%.1f hum=%.0f", co2, tmp, hum);
    }

    // set state
    sns_state.co2 = co2;
    sns_state.tmp = tmp;
    sns_state.hum = hum;

    // release mutex
    naos_unlock(sns_mutex);

    // trigger signal
    naos_trigger(sns_signal, 1, false);

    // dispatch event
    sig_dispatch(SIG_SENSOR);
  }
}

void sns_init() {
  // create mutex
  sns_mutex = naos_mutex();

  // create signal
  sns_signal = naos_signal();

  // wait at least one second
  uint32_t ms = naos_millis();
  if (ms < 1100) {
    if (SNS_DEBUG) {
      naos_log("sns: delay init by %dms", 1100 - ms);
    }
    naos_delay(1100 - ms);
  }

  // wake up
  sns_transfer(0x36f6, 0, 0, true);

  // stop periodic measurement
  sns_transfer(0x3f86, 0, 0, true);
  naos_delay(500);

  // read serial
  if (SNS_DEBUG) {
    sns_transfer(0x3682, 0, 3, false);
    naos_log("sns: serial %lu %lu %lu", sns_read[0], sns_read[1], sns_read[2]);
  }

  // start periodic measurement
  sns_transfer(0x21b1, 0, 0, false);

  // run check task
  naos_run("sns", 8192, 1, sns_check);
}

void sns_set(bool on) {
  if (on) {
    // wake up
    sns_transfer(0x36f6, 0, 0, false);

    // start periodic measurement
    sns_transfer(0x21b1, 0, 0, false);
  } else {
    // stop periodic measurement
    sns_transfer(0x3f86, 0, 0, false);
    naos_delay(500);

    // power down
    sns_transfer(0x36e0, 0, 0, false);
  }
}

sns_state_t sns_get() {
  // get state
  naos_lock(sns_mutex);
  sns_state_t state = sns_state;
  naos_unlock(sns_mutex);

  return state;
}

sns_state_t sns_next() {
  // await signal
  naos_await(sns_signal, 1, true);

  // get state
  sns_state_t state = sns_get();

  return state;
}
