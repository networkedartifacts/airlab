#include <stdbool.h>

#include "sensor_hal.h"

// Chips: SCD41, SGP41, LPS22HB

#define AL_SENSOR_HAL_SCD41 0x62
#define AL_SENSOR_HAL_SGP41 0x59
#define AL_SENSOR_HAL_LPS22 0x5C

#define AL_SENSOR_MSR_TIME 5000  // ms

#define AL_CHECK(call)              \
  {                                 \
    al_sensor_hal_err_t err = call; \
    if (err != AL_SENSOR_HAL_OK) {  \
      return err;                   \
    }                               \
  }

static al_sensor_hal_ops_t al_sensor_hal_ops;
static al_sensor_hal_state_t* al_sensor_hal_state;
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

static al_sensor_hal_err_t al_sensor_hal_transfer(uint8_t target, uint16_t addr, size_t send, size_t receive,
                                                  bool may_fail) {
  // prepare write length
  size_t write = 0;

  // prepare flag
  al_sensor_hal_err_t flag = 0;
  if (target == AL_SENSOR_HAL_SCD41) {
    flag |= AL_SENSOR_HAL_ERR_SCD41;
  } else if (target == AL_SENSOR_HAL_SGP41) {
    flag |= AL_SENSOR_HAL_ERR_SGP41;
  }

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
  al_sensor_hal_err_t err = al_sensor_hal_ops.transfer(target, al_sensor_hal_bt, write, al_sensor_hal_bt, receive * 3);
  if (err != AL_SENSOR_HAL_OK && !may_fail) {
    return err | flag;
  } else if (err != AL_SENSOR_HAL_OK && may_fail) {
    return AL_SENSOR_HAL_OK;
  }

  // read bytes
  for (size_t i = 0; i < receive; i++) {
    al_sensor_hal_br[i] = (al_sensor_hal_bt[i * 3] << 8) | al_sensor_hal_bt[i * 3 + 1];
    uint8_t crc = al_sensor_hal_crc(al_sensor_hal_bt + (i * 3), 2);
    if (al_sensor_hal_bt[i * 3 + 2] != crc) {
      return AL_SENSOR_HAL_ERR_CHECKSUM | flag;
    }
  }

  return AL_SENSOR_HAL_OK;
}

static al_sensor_hal_err_t al_sensor_hal_read_lps(uint8_t reg, uint8_t* val) {
  // read register
  al_sensor_hal_err_t err = al_sensor_hal_ops.transfer(AL_SENSOR_HAL_LPS22, &reg, 1, val, 1);
  if (err != AL_SENSOR_HAL_OK) {
    return err | AL_SENSOR_HAL_ERR_LPS22;
  }

  return AL_SENSOR_HAL_OK;
}

static al_sensor_hal_err_t al_sensor_hal_write_lps(uint8_t reg, uint8_t val) {
  // write register
  al_sensor_hal_bt[0] = reg;
  al_sensor_hal_bt[1] = val;
  al_sensor_hal_err_t err = al_sensor_hal_ops.transfer(AL_SENSOR_HAL_LPS22, al_sensor_hal_bt, 2, NULL, 0);
  if (err != AL_SENSOR_HAL_OK) {
    return err | AL_SENSOR_HAL_ERR_LPS22;
  }

  return AL_SENSOR_HAL_OK;
}

static al_sensor_hal_err_t al_sensor_hal_measure() {
  // wake up SCD41
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD41, 0x36f6, 0, 0, true));
  al_sensor_hal_ops.delay(30);

  // initiate single-shot measurement
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD41, 0x219d, 0, 0, false));

  return AL_SENSOR_HAL_OK;
}

void al_sensor_hal_init(al_sensor_hal_ops_t ops, al_sensor_hal_state_t* state) {
  // store ops and state
  al_sensor_hal_ops = ops;
  al_sensor_hal_state = state;
}

al_sensor_hal_err_t al_sensor_hal_config(al_sensor_hal_mode_t mode, int interval) {
  // wake up SCD
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD41, 0x36f6, 0, 0, true));
  al_sensor_hal_ops.delay(30);

  // stop SCD periodic measurement
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD41, 0x3f86, 0, 0, true));
  al_sensor_hal_ops.delay(500);

  // apply SCD sensor mode
  if (mode == AL_SENSOR_HAL_NORMAL) {
    // start periodic measurement
    AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD41, 0x21b1, 0, 0, false));
  } else if (mode == AL_SENSOR_HAL_LOW_POWER) {
    // start low power periodic measurement
    AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD41, 0x21ac, 0, 0, false));
  } else if (mode == AL_SENSOR_HAL_SLEEP) {
    // power down
    AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD41, 0x36e0, 0, 0, false));
  } else if (mode != AL_SENSOR_HAL_MANUAL) {
    return AL_SENSOR_HAL_ERR_MODE;
  }

  // turn off SGP heater when sleeping
  if (mode == AL_SENSOR_HAL_SLEEP) {
    AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SGP41, 0x3615, 0, 0, false));
  }

  // configure LPS sensor
  if (mode == AL_SENSOR_HAL_SLEEP) {
    AL_CHECK(al_sensor_hal_write_lps(0x10, 0x0));  // power down
  } else {
    AL_CHECK(al_sensor_hal_write_lps(0x10, 0x18));  // 1Hz, LPF on
  }

  // store mode
  al_sensor_hal_state->mode = mode;
  al_sensor_hal_state->interval = interval;

  return AL_SENSOR_HAL_OK;
}

al_sensor_hal_err_t al_sensor_hal_ready() {
  // handle manual mode
  if (al_sensor_hal_state->mode == AL_SENSOR_HAL_MANUAL) {
    // ensure next measurement if zero
    if (al_sensor_hal_state->next == 0) {
      al_sensor_hal_state->next = al_sensor_hal_ops.epoch() + al_sensor_hal_state->interval;
    }

    // limit measurement deadline to current interval
    if (al_sensor_hal_state->next > al_sensor_hal_ops.epoch() + al_sensor_hal_state->interval) {
      al_sensor_hal_state->next = al_sensor_hal_ops.epoch() + al_sensor_hal_state->interval;
    }

    // take measurement if deadline is reached
    if (al_sensor_hal_ops.epoch() >= al_sensor_hal_state->next - AL_SENSOR_MSR_TIME) {
      AL_CHECK(al_sensor_hal_measure());
      al_sensor_hal_state->next = al_sensor_hal_ops.epoch() + al_sensor_hal_state->interval;
    }
  }

  // otherwise, check if SCD measurement is available
  al_sensor_hal_err_t err = al_sensor_hal_transfer(AL_SENSOR_HAL_SCD41, 0xe4b8, 0, 1, false);
  if (err != AL_SENSOR_HAL_OK || (al_sensor_hal_br[0] & 0xFFF) == 0) {
    return AL_SENSOR_HAL_BUSY;
  }

  return AL_SENSOR_HAL_OK;
}

al_sensor_hal_err_t al_sensor_hal_read(al_sensor_hal_data_t* data) {
  // read SCD sensor
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SCD41, 0xec05, 0, 3, false));
  data->co2 = al_sensor_hal_br[0];
  data->tmp = al_sensor_hal_br[1];
  data->hum = al_sensor_hal_br[2];

  // read SGP sensor
  al_sensor_hal_bw[0] = data->hum;
  al_sensor_hal_bw[1] = data->tmp;
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SGP41, 0x2619, 2, 0, false));
  al_sensor_hal_ops.delay(50);
  AL_CHECK(al_sensor_hal_transfer(AL_SENSOR_HAL_SGP41, 0, 0, 2, false));
  data->voc = al_sensor_hal_br[0];
  data->nox = al_sensor_hal_br[1];

  // read LPS sensor
  AL_CHECK(al_sensor_hal_read_lps(0x28, &al_sensor_hal_bt[0]));
  AL_CHECK(al_sensor_hal_read_lps(0x29, &al_sensor_hal_bt[1]));
  AL_CHECK(al_sensor_hal_read_lps(0x2a, &al_sensor_hal_bt[2]));
  data->prs = al_sensor_hal_bt[0] | al_sensor_hal_bt[1] << 8 | al_sensor_hal_bt[2] << 16;

  // set epoch
  data->epoch = al_sensor_hal_ops.epoch();

  // clear deadline
  al_sensor_hal_state->next = 0;

  // set next measurement in manual mode
  if (al_sensor_hal_state->mode == AL_SENSOR_HAL_MANUAL) {
    al_sensor_hal_state->next = al_sensor_hal_ops.epoch() + al_sensor_hal_state->interval;
  }

  return AL_SENSOR_HAL_OK;
}

al_sensor_hal_state_t al_sensor_hal_dump() {
  // return state
  return *al_sensor_hal_state;
}
