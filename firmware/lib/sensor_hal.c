#include "sensor_hal.h"

#define AL_SENSOR_HAL_SCD 0x62
#define AL_SENSOR_HAL_SGP 0x59
#define AL_SENSOR_HAL_LPS 0x5C
#define AL_SENSOR_HAL_DEBUG false

#define AL_CHECK(call) \
  if (!call) {         \
    return false;      \
  }

static al_sensor_hal_ops_t al_sensor_hal_ops;
static uint16_t al_sensor_hal_bw[4];
static uint16_t al_sensor_hal_br[4];
static uint8_t al_sensor_hal_bt[2 + 4 * 3];

static uint8_t al_sensor_hal_crc(const uint8_t* data, uint16_t count) {
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

static bool al_sensor_hal_transfer(uint8_t target, uint16_t addr, size_t send, size_t receive, bool may_fail) {
  // prepare write length
  size_t write = 0;

  // write address
  if (addr != 0) {
    al_sensor_hal_bt[0] = addr >> 8;
    al_sensor_hal_bt[1] = addr & 0xFF;
    write += 2;
  }

  // write bytes
  for (size_t i = 0; i < send; i++) {
    al_sensor_hal_bt[2 + i * 3] = al_sensor_hal_bw[i] >> 8;
    al_sensor_hal_bt[2 + i * 3 + 1] = al_sensor_hal_bw[i] & 0xFF;
    al_sensor_hal_bt[2 + i * 3 + 2] = al_sensor_hal_crc(al_sensor_hal_bt + (2 + i * 3), 2);
    write += 3;
  }

  // run command
  bool ok = al_sensor_hal_ops.transfer(target, al_sensor_hal_bt, write, al_sensor_hal_bt, receive * 3);
  if (!ok && !may_fail) {
    al_sensor_hal_ops.debug("transfer failed");
    return false;
  }

  // skip verify if may fail
  if (may_fail) {
    return true;
  }

  // read bytes
  for (size_t i = 0; i < receive; i++) {
    al_sensor_hal_br[i] = (al_sensor_hal_bt[i * 3] << 8) | al_sensor_hal_bt[i * 3 + 1];
    uint8_t crc = al_sensor_hal_crc(al_sensor_hal_bt + (i * 3), 2);
    if (al_sensor_hal_bt[i * 3 + 2] != crc) {
      al_sensor_hal_ops.debug("crc mismatch");
      return false;
    }
  }

  return true;
}

static bool al_sensor_hal_read_lps(uint8_t reg, uint8_t* val) {
  // read register
  bool ok = al_sensor_hal_ops.transfer(AL_SENSOR_HAL_LPS, &reg, 1, val, 1);

  return ok;
}

static bool al_sensor_hal_write_lps(uint8_t reg, uint8_t val) {
  // write register
  al_sensor_hal_bt[0] = reg;
  al_sensor_hal_bt[1] = val;
  bool ok = al_sensor_hal_ops.transfer(AL_SENSOR_HAL_LPS, al_sensor_hal_bt, 2, NULL, 0);

  return ok;
}

void al_sensor_hal_wire(al_sensor_hal_ops_t ops) {
  // store ops
  al_sensor_hal_ops = ops;
}

bool al_sensor_hal_reset() {
  // wake up SCD
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD, 0x36f6, 0, 0, true));

  // stop SDC periodic measurement
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD, 0x3f86, 0, 0, true));
  al_sensor_hal_ops.delay(500);

  // start SCD periodic measurement
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD, 0x21b1, 0, 0, false));

  // start LPS periodic measurement (10Hz, LPF on)
  AL_CHECK(al_sensor_hal_write_lps(0x10, 0x28));

  // condition SGP sensor
  // al_sensor_write[0] = 0x8000;
  // al_sensor_write[1] = 0x6666;
  // al_sensor_transfer(AL_SENSOR_SGP, 0x2612, 2, 1, false);

  return true;
}

bool al_sensor_hal_ready() {
  // check if SCD measurement is available
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD, 0xe4b8, 0, 1, false));
  if ((al_sensor_hal_br[0] & 0xFFF) == 0) {
    return false;
  }

  return true;
}

bool al_sensor_hal_read(al_sensor_hal_data_t* data) {
  // read SCD sensor
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD, 0xec05, 0, 3, false));
  data->co2 = al_sensor_hal_br[0];
  data->tmp = al_sensor_hal_br[1];
  data->hum = al_sensor_hal_br[2];

  // read SGP sensor
  al_sensor_hal_bw[0] = data->hum;
  al_sensor_hal_bw[1] = data->tmp;
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SGP, 0x2619, 2, 0, false));
  al_sensor_hal_ops.delay(50);
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SGP, 0, 0, 2, false));
  data->voc = al_sensor_hal_br[0];
  data->nox = al_sensor_hal_br[1];

  // read LPS sensor
  AL_CHECK(al_sensor_hal_read_lps(0x28, &al_sensor_hal_bt[0]));
  AL_CHECK(al_sensor_hal_read_lps(0x29, &al_sensor_hal_bt[1]));
  AL_CHECK(al_sensor_hal_read_lps(0x2a, &al_sensor_hal_bt[2]));
  data->prs = al_sensor_hal_bt[0] | al_sensor_hal_bt[1] << 8 | al_sensor_hal_bt[2] << 16;

  return true;
}

bool al_sensor_hal_sleep() {
  // stop periodic measurement
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD, 0x3f86, 0, 0, false));
  al_sensor_hal_ops.delay(500);

  // power down SCD
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD, 0x36e0, 0, 0, false));

  // TODO: Is turn off the SGP sensor ok?

  // turn off SGP
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SGP, 0x3615, 0, 0, false));

  return true;
}

bool al_sensor_hal_wake() {
  // wake up SCD
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD, 0x36f6, 0, 0, false));

  // start SCD periodic measurement
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD, 0x21b1, 0, 0, false));

  return true;
}
