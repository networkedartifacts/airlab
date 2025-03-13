#include <naos.h>
#include <naos/sys.h>
#include <driver/i2c.h>

#include "sns.h"
#include "sig.h"
#include "sns_gas.h"

#define SNS_SCD 0x62
#define SNS_SGP 0x59
#define SNS_LPS 0x5C
#define SNS_DEBUG false

// TODO: Support low power measurement mode (30s).
// TODO: Perform SGP41 conditioning (10s).
// TODO: Update gas index sampling interval dynamically?

static naos_mutex_t sns_mutex;
static naos_signal_t sns_signal;
static sns_state_t sns_history[SNS_HIST] = {0};
static size_t sns_pos = 0;
static uint16_t sns_write[8];
static uint16_t sns_read[8];
static uint8_t sns_buffer[24];
static GasIndexAlgorithmParams sns_voc_params;
static GasIndexAlgorithmParams sns_nox_params;

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

void sns_to_ticks(float rh_in, float t_in, uint16_t* rh_out, uint16_t* t_out) {
  // convert temperature and humidity to ticks
  *rh_out = (uint16_t)(rh_in * 65535.f / 100.f);
  *t_out = (uint16_t)((t_in + 45.f) * 65535.f / 175.f);
}

static void sns_transfer(uint8_t target, uint16_t addr, size_t send, size_t receive, bool may_fail) {
  // write address
  sns_buffer[0] = addr >> 8;
  sns_buffer[1] = addr & 0xFF;

  // write bytes
  for (size_t i = 0; i < send; i++) {
    sns_buffer[2 + i * 3] = sns_write[i] >> 8;
    sns_buffer[2 + i * 3 + 1] = sns_write[i] & 0xFF;
    sns_buffer[2 + i * 3 + 2] = sns_crc(sns_buffer + (2 + i * 3), 2);
  }

  // run command
  esp_err_t err;
  if (receive > 0) {
    err = i2c_master_write_read_device(I2C_NUM_0, target, sns_buffer, 2 + send * 3, sns_buffer, receive * 3, 1000);
  } else {
    err = i2c_master_write_to_device(I2C_NUM_0, target, sns_buffer, 2 + send * 3, 1000);
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

static void sns_receive(uint8_t target, size_t receive) {
  // run command
  esp_err_t err = i2c_master_read_from_device(I2C_NUM_0, target, sns_buffer, receive * 3, 1000);
  ESP_ERROR_CHECK_WITHOUT_ABORT(err);
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

static uint8_t sns_read_lps(uint8_t reg) {
  uint8_t res = 0;
  ESP_ERROR_CHECK(i2c_master_write_read_device(I2C_NUM_0, SNS_LPS, &reg, 1, &res, 1, 1000));

  return res;
}

static void sns_write_lps(uint8_t reg, uint8_t val) {
  uint8_t data[2] = {reg, val};
  ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, SNS_LPS, data, 2, 1000));
}

static void sns_check() {
  for (;;) {
    // wait a second
    naos_delay(1000);

    // acquire mutex
    naos_lock(sns_mutex);

    // check if SCD measurement is available
    sns_transfer(SNS_SCD, 0xe4b8, 0, 1, false);
    if ((sns_read[0] & 0xFFF) == 0) {
      naos_unlock(sns_mutex);
      continue;
    }

    // read SCD sensor
    sns_transfer(SNS_SCD, 0xec05, 0, 3, false);

    // calculate values
    float co2 = (float)sns_read[0];                                          // ppm
    float tmp = -45.f + 175.f * ((float)sns_read[1] / (float)(UINT16_MAX));  // °C
    float hum = 100.f * ((float)sns_read[2] / (float)(UINT16_MAX));          // % rH
    if (SNS_DEBUG) {
      naos_log("sns: SCD values: co2=%.0f tmp=%.1f hum=%.1f", co2, tmp, hum);
    }

    // prepare compensation values
    sns_to_ticks(hum, tmp, &sns_write[0], &sns_write[1]);
    if (SNS_DEBUG) {
      naos_log("sns: SCD ticks: rh=%u t=%u", sns_write[0], sns_write[1]);
    }

    // read SGP sensor
    sns_transfer(SNS_SGP, 0x2619, 2, 0, false);
    naos_delay(50);
    sns_receive(SNS_SGP, 2);
    if (SNS_DEBUG) {
      naos_log("sns: SGP ticks: voc=%u nox=%u", sns_read[0], sns_read[1]);
    }

    // update sampling interval
    // gas_voc_params.mSamplingInterval = input.delta;
    // gas_nox_params.mSamplingInterval = input.delta;

    // perform gas index calculation
    int32_t voc_index = 0;
    int32_t nox_index = 0;
    GasIndexAlgorithm_process(&sns_voc_params, sns_read[0], &voc_index);
    GasIndexAlgorithm_process(&sns_nox_params, sns_read[1], &nox_index);

    // calculate values
    if (SNS_DEBUG) {
      naos_log("sns: SGP values: voc=%d nox=%d", voc_index, nox_index);
    }

    // read LPS pressure
    float pressure = (sns_read_lps(0x28) | (sns_read_lps(0x29) << 8) | (sns_read_lps(0x2a) << 16)) / 4096.f;
    if (SNS_DEBUG) {
      naos_log("sns: LPS pressure: %.2f hPa", pressure);
    }

    // advanced
    sns_pos++;
    if (sns_pos >= SNS_HIST) {
      sns_pos = 0;
    }

    // set state
    sns_history[sns_pos].ok = true;
    sns_history[sns_pos].co2 = co2;
    sns_history[sns_pos].tmp = tmp;
    sns_history[sns_pos].hum = hum;
    sns_history[sns_pos].voc = (float)voc_index;
    sns_history[sns_pos].nox = (float)nox_index;

    // release mutex
    naos_unlock(sns_mutex);

    // trigger signal
    naos_trigger(sns_signal, 1, false);

    // dispatch event
    sig_dispatch((sig_event_t){
        .type = SIG_SENSOR,
    });
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

  // wake up SCD
  sns_transfer(SNS_SCD, 0x36f6, 0, 0, true);

  // stop SDC periodic measurement
  sns_transfer(SNS_SCD, 0x3f86, 0, 0, true);
  naos_delay(500);

  // read serials
  if (SNS_DEBUG) {
    sns_transfer(SNS_SCD, 0x3682, 0, 3, false);
    naos_log("sns: SCD serial %lu %lu %lu", sns_read[0], sns_read[1], sns_read[2]);
    sns_transfer(SNS_SGP, 0x3682, 0, 3, false);
    naos_log("sns: SGP serial %lu %lu %lu", sns_read[0], sns_read[1], sns_read[2]);
    uint8_t lps = sns_read_lps(0x0F);
    naos_log("sns: LPS serial %u", lps);
  }

  // initialize gas index parameters
  GasIndexAlgorithm_init_with_sampling_interval(&sns_voc_params, GasIndexAlgorithm_ALGORITHM_TYPE_VOC, 5.f);
  GasIndexAlgorithm_init_with_sampling_interval(&sns_nox_params, GasIndexAlgorithm_ALGORITHM_TYPE_NOX, 5.f);

  // start SCD periodic measurement
  sns_transfer(SNS_SCD, 0x21b1, 0, 0, false);

  // start LPS periodic measurement (10Hz, LPF on)
  sns_write_lps(0x10, 0x28);

  // condition SGP sensor
  // sns_write[0] = 0x8000;
  // sns_write[1] = 0x6666;
  // sns_transfer(SNS_SGP, 0x2612, 2, 1, false);

  // run check task
  naos_run("sns", 8192, 1, sns_check);
}

void sns_set(bool on) {
  if (on) {
    // wake up SCD
    sns_transfer(SNS_SCD, 0x36f6, 0, 0, false);

    // start SCD periodic measurement
    sns_transfer(SNS_SCD, 0x21b1, 0, 0, false);
  } else {
    // stop periodic measurement
    sns_transfer(SNS_SCD, 0x3f86, 0, 0, false);
    naos_delay(500);

    // power down SCD
    sns_transfer(SNS_SCD, 0x36e0, 0, 0, false);

    // TODO: Is turn off the SGP sensor ok?

    // turn off SGP
    sns_transfer(SNS_SGP, 0x3615, 0, 0, false);
  }
}

sns_state_t sns_get() {
  // get state
  naos_lock(sns_mutex);
  sns_state_t state = sns_history[sns_pos];
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

sns_hist_t sns_query(sns_mode_t mode) {
  // prepare history
  sns_hist_t hist = {0};

  // copy values
  for (size_t i = 0; i < SNS_HIST; i++) {
    size_t pos = (sns_pos + 1 + i) % SNS_HIST;
    switch (mode) {
      case SNS_CO2:
        hist.values[i] = sns_history[pos].co2;
        break;
      case SNS_TMP:
        hist.values[i] = sns_history[pos].tmp;
        break;
      case SNS_HUM:
        hist.values[i] = sns_history[pos].hum;
        break;
      case SNS_VOC:
        hist.values[i] = sns_history[pos].voc;
        break;
      case SNS_NOX:
        hist.values[i] = sns_history[pos].nox;
        break;
    }
  }

  // calculate min/max
  hist.min = 9999.f;
  for (size_t i = 0; i < SNS_HIST; i++) {
    if (hist.values[i] > hist.max) {
      hist.max = hist.values[i];
    }
    if (hist.values[i] < hist.min) {
      hist.min = hist.values[i];
    }
  }

  return hist;
}
