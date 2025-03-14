#include <naos.h>
#include <naos/sys.h>
#include <driver/i2c.h>

#include "sensor_hal.h"

#define AL_SENSOR_SCD 0x62
#define AL_SENSOR_SGP 0x59
#define AL_SENSOR_LPS 0x5C
#define AL_SENSOR_DEBUG false

static uint16_t al_sensor_buf_write[8];
static uint16_t al_sensor_buf_read[8];
static uint8_t al_sensor_buf_transfer[24];

static uint8_t al_sensor_crc(const uint8_t* data, uint16_t count) {
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

static void al_sensor_transfer(uint8_t target, uint16_t addr, size_t send, size_t receive, bool may_fail) {
  // write address
  al_sensor_buf_transfer[0] = addr >> 8;
  al_sensor_buf_transfer[1] = addr & 0xFF;

  // write bytes
  for (size_t i = 0; i < send; i++) {
    al_sensor_buf_transfer[2 + i * 3] = al_sensor_buf_write[i] >> 8;
    al_sensor_buf_transfer[2 + i * 3 + 1] = al_sensor_buf_write[i] & 0xFF;
    al_sensor_buf_transfer[2 + i * 3 + 2] = al_sensor_crc(al_sensor_buf_transfer + (2 + i * 3), 2);
  }

  // run command
  esp_err_t err;
  if (receive > 0) {
    err = i2c_master_write_read_device(I2C_NUM_0, target, al_sensor_buf_transfer, 2 + send * 3, al_sensor_buf_transfer,
                                       receive * 3, 1000);
  } else {
    err = i2c_master_write_to_device(I2C_NUM_0, target, al_sensor_buf_transfer, 2 + send * 3, 1000);
  }
  if (!may_fail) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);
  }
  if (err != ESP_OK) {
    return;
  }

  // read bytes
  for (size_t i = 0; i < receive; i++) {
    al_sensor_buf_read[i] = (al_sensor_buf_transfer[i * 3] << 8) | al_sensor_buf_transfer[i * 3 + 1];
    uint8_t crc = al_sensor_crc(al_sensor_buf_transfer + (i * 3), 2);
    if (al_sensor_buf_transfer[i * 3 + 2] != crc) {
      naos_log("snd: crc failed in=%u crc=%u", al_sensor_buf_transfer[i * 3 + 2], crc);
      ESP_ERROR_CHECK_WITHOUT_ABORT(ESP_FAIL);
    }
  }
}

static void al_sensor_receive(uint8_t target, size_t receive) {
  // run command
  esp_err_t err = i2c_master_read_from_device(I2C_NUM_0, target, al_sensor_buf_transfer, receive * 3, 1000);
  ESP_ERROR_CHECK_WITHOUT_ABORT(err);
  if (err != ESP_OK) {
    return;
  }

  // read bytes
  for (size_t i = 0; i < receive; i++) {
    al_sensor_buf_read[i] = (al_sensor_buf_transfer[i * 3] << 8) | al_sensor_buf_transfer[i * 3 + 1];
    uint8_t crc = al_sensor_crc(al_sensor_buf_transfer + (i * 3), 2);
    if (al_sensor_buf_transfer[i * 3 + 2] != crc) {
      naos_log("snd: crc failed in=%u crc=%u", al_sensor_buf_transfer[i * 3 + 2], crc);
      ESP_ERROR_CHECK_WITHOUT_ABORT(ESP_FAIL);
    }
  }
}

void al_sensor_to_ticks(float rh_in, float t_in, uint16_t* rh_out, uint16_t* t_out) {
  // convert temperature and humidity to ticks
  *rh_out = (uint16_t)(rh_in * 65535.f / 100.f);
  *t_out = (uint16_t)((t_in + 45.f) * 65535.f / 175.f);
}

static uint8_t al_sensor_read_lps(uint8_t reg) {
  uint8_t res = 0;
  ESP_ERROR_CHECK(i2c_master_write_read_device(I2C_NUM_0, AL_SENSOR_LPS, &reg, 1, &res, 1, 1000));

  return res;
}

static void al_sensor_write_lps(uint8_t reg, uint8_t val) {
  uint8_t data[2] = {reg, val};
  ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, AL_SENSOR_LPS, data, 2, 1000));
}

void al_sensor_reset() {
  // wait at least one second
  uint32_t ms = naos_millis();
  if (ms < 1100) {
    if (AL_SENSOR_DEBUG) {
      naos_log("sns: delay init by %dms", 1100 - ms);
    }
    naos_delay(1100 - ms);
  }

  // wake up SCD
  al_sensor_transfer(AL_SENSOR_SCD, 0x36f6, 0, 0, true);

  // stop SDC periodic measurement
  al_sensor_transfer(AL_SENSOR_SCD, 0x3f86, 0, 0, true);
  naos_delay(500);

  // read serials
  if (AL_SENSOR_DEBUG) {
    al_sensor_transfer(AL_SENSOR_SCD, 0x3682, 0, 3, false);
    naos_log("sns: SCD serial %lu %lu %lu", al_sensor_buf_read[0], al_sensor_buf_read[1], al_sensor_buf_read[2]);
    al_sensor_transfer(AL_SENSOR_SGP, 0x3682, 0, 3, false);
    naos_log("sns: SGP serial %lu %lu %lu", al_sensor_buf_read[0], al_sensor_buf_read[1], al_sensor_buf_read[2]);
    uint8_t lps = al_sensor_read_lps(0x0F);
    naos_log("sns: LPS serial %u", lps);
  }

  // start SCD periodic measurement
  al_sensor_transfer(AL_SENSOR_SCD, 0x21b1, 0, 0, false);

  // start LPS periodic measurement (10Hz, LPF on)
  al_sensor_write_lps(0x10, 0x28);

  // condition SGP sensor
  // al_sensor_write[0] = 0x8000;
  // al_sensor_write[1] = 0x6666;
  // al_sensor_transfer(AL_SENSOR_SGP, 0x2612, 2, 1, false);
}

bool al_sensor_ready() {
  // check if SCD measurement is available
  al_sensor_transfer(AL_SENSOR_SCD, 0xe4b8, 0, 1, false);
  if ((al_sensor_buf_read[0] & 0xFFF) == 0) {
    return false;
  }

  return true;
}

al_sensor_raw_t al_sensor_read() {
  // read SCD sensor
  al_sensor_transfer(AL_SENSOR_SCD, 0xec05, 0, 3, false);

  // calculate values
  float co2 = (float)al_sensor_buf_read[0];                                          // ppm
  float tmp = -45.f + 175.f * ((float)al_sensor_buf_read[1] / (float)(UINT16_MAX));  // °C
  float hum = 100.f * ((float)al_sensor_buf_read[2] / (float)(UINT16_MAX));          // % rH
  if (AL_SENSOR_DEBUG) {
    naos_log("sns: SCD values: co2=%.0f tmp=%.1f hum=%.1f", co2, tmp, hum);
  }

  // prepare compensation values
  al_sensor_to_ticks(hum, tmp, &al_sensor_buf_write[0], &al_sensor_buf_write[1]);
  if (AL_SENSOR_DEBUG) {
    naos_log("sns: SCD ticks: rh=%u t=%u", al_sensor_buf_write[0], al_sensor_buf_write[1]);
  }

  // read SGP sensor
  al_sensor_transfer(AL_SENSOR_SGP, 0x2619, 2, 0, false);
  naos_delay(50);
  al_sensor_receive(AL_SENSOR_SGP, 2);

  // get values
  uint16_t voc = al_sensor_buf_read[0];
  uint16_t nox = al_sensor_buf_read[1];
  if (AL_SENSOR_DEBUG) {
    naos_log("sns: SGP ticks: voc=%u nox=%u", voc, nox);
  }

  // read LPS pressure
  float prs = (al_sensor_read_lps(0x28) | (al_sensor_read_lps(0x29) << 8) | (al_sensor_read_lps(0x2a) << 16)) / 4096.f;
  if (AL_SENSOR_DEBUG) {
    naos_log("sns: LPS pressure: %.2f hPa", prs);
  }

  // create raw
  al_sensor_raw_t raw = {
      .co2 = co2,
      .tmp = tmp,
      .hum = hum,
      .voc = voc,
      .nox = nox,
      .prs = prs,
  };

  return raw;
}

void al_sensor_sleep() {
  // stop periodic measurement
  al_sensor_transfer(AL_SENSOR_SCD, 0x3f86, 0, 0, false);
  naos_delay(500);

  // power down SCD
  al_sensor_transfer(AL_SENSOR_SCD, 0x36e0, 0, 0, false);

  // TODO: Is turn off the SGP sensor ok?

  // turn off SGP
  al_sensor_transfer(AL_SENSOR_SGP, 0x3615, 0, 0, false);
}

void al_sensor_wake() {
  // wake up SCD
  al_sensor_transfer(AL_SENSOR_SCD, 0x36f6, 0, 0, false);

  // start SCD periodic measurement
  al_sensor_transfer(AL_SENSOR_SCD, 0x21b1, 0, 0, false);
}
