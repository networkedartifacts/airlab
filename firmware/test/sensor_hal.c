#include <string.h>

#include <unity.h>

// include the unit directly to access its static state for resets
#include "../lib/sensor_hal.c"

/* Fake Devices */

// a minimal model of the three sensor chips served over the injected transfer
// op: a transaction log for sequence assertions, canned measurement data, and
// knobs to fail transfers and corrupt response checksums

typedef struct {
  uint8_t target;
  uint16_t cmd;  // 16-bit command, first register or zero for bare reads
  size_t wl;
  size_t rl;
} fake_tx_t;

static fake_tx_t fake_txs[128];
static int fake_txs_len;

static int64_t fake_epoch;
static uint32_t fake_delayed;

static uint16_t scd_ready_word;
static uint16_t scd_data[3];
static uint16_t scd_offset;
static uint16_t sgp_data[2];
static uint16_t sgp_comp[2];
static uint8_t lps_regs[0x40];

static uint8_t fail_target;
static int fail_count;
static bool corrupt_next;

// independent copy of the datasheet CRC-8 (poly 0x31, init 0xFF)
static uint8_t fake_crc(const uint8_t* data, int count) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < count; i++) {
    crc ^= data[i];
    for (int bit = 8; bit > 0; bit--) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static void fake_put_words(uint8_t* rd, const uint16_t* words, size_t num) {
  for (size_t i = 0; i < num; i++) {
    rd[i * 3] = words[i] >> 8;
    rd[i * 3 + 1] = words[i] & 0xFF;
    rd[i * 3 + 2] = fake_crc(rd + i * 3, 2);
  }
  if (corrupt_next && num > 0) {
    rd[2] ^= 0xFF;
    corrupt_next = false;
  }
}

static uint16_t fake_get_word(const uint8_t* wd, size_t index) {
  // validate the payload checksum written by the unit
  TEST_ASSERT_EQUAL_HEX8(fake_crc(wd + 2 + index * 3, 2), wd[2 + index * 3 + 2]);
  return (uint16_t)(wd[2 + index * 3] << 8 | wd[2 + index * 3 + 1]);
}

static al_sensor_hal_err_t fake_transfer(uint8_t target, uint8_t* wd, size_t wl, uint8_t* rd, size_t rl) {
  // log transaction
  uint16_t cmd = 0;
  if (target == AL_SENSOR_HAL_LPS22) {
    cmd = wl > 0 ? wd[0] : 0;
  } else if (wl >= 2) {
    cmd = (uint16_t)(wd[0] << 8 | wd[1]);
  }
  TEST_ASSERT(fake_txs_len < (int)(sizeof(fake_txs) / sizeof(fake_txs[0])));
  fake_txs[fake_txs_len++] = (fake_tx_t){.target = target, .cmd = cmd, .wl = wl, .rl = rl};

  // fail transfer if armed
  if (fail_count > 0 && target == fail_target) {
    fail_count--;
    return AL_SENSOR_HAL_ERR_TRANSFER;
  }

  // handle LPS22 raw register access
  if (target == AL_SENSOR_HAL_LPS22) {
    if (rl > 0) {
      TEST_ASSERT(cmd + rl <= sizeof(lps_regs));
      memcpy(rd, &lps_regs[cmd], rl);
    } else {
      TEST_ASSERT_EQUAL_size_t(2, wl);
      lps_regs[wd[0]] = wd[1];
    }
    return AL_SENSOR_HAL_OK;
  }

  // handle SCD41 commands
  if (target == AL_SENSOR_HAL_SCD41) {
    if (cmd == 0x241d) {
      scd_offset = fake_get_word(wd, 0);
    } else if (cmd == 0xe4b8) {
      fake_put_words(rd, &scd_ready_word, 1);
    } else if (cmd == 0xec05) {
      fake_put_words(rd, scd_data, 3);
      scd_ready_word = 0;  // reading clears the data-ready flag
    }
    return AL_SENSOR_HAL_OK;
  }

  // handle SGP41 commands
  if (target == AL_SENSOR_HAL_SGP41) {
    if (cmd == 0x2612 || cmd == 0x2619) {
      sgp_comp[0] = fake_get_word(wd, 0);
      sgp_comp[1] = fake_get_word(wd, 1);
    } else if (wl == 0) {
      fake_put_words(rd, sgp_data, 2);
    }
    return AL_SENSOR_HAL_OK;
  }

  TEST_FAIL_MESSAGE("unexpected target");
  return AL_SENSOR_HAL_ERR_TRANSFER;
}

static void fake_delay(uint32_t ms) { fake_delayed += ms; }

static int64_t fake_epoch_fn() { return fake_epoch; }

/* Helpers */

static al_sensor_hal_state_t hal_state;

static void hal_reset(bool condition) {
  // reset fakes
  fake_txs_len = 0;
  fake_epoch = 1000000;
  fake_delayed = 0;
  scd_ready_word = 0;
  memset(scd_data, 0, sizeof(scd_data));
  scd_offset = 0;
  memset(sgp_data, 0, sizeof(sgp_data));
  memset(sgp_comp, 0, sizeof(sgp_comp));
  memset(lps_regs, 0, sizeof(lps_regs));
  fail_target = 0;
  fail_count = 0;
  corrupt_next = false;

  // reset unit state
  memset(&hal_state, 0, sizeof(hal_state));
  al_sensor_hal_init(
      (al_sensor_hal_ops_t){
          .transfer = fake_transfer,
          .delay = fake_delay,
          .epoch = fake_epoch_fn,
          .condition = condition,
      },
      &hal_state);
}

static void assert_tx(int index, uint8_t target, uint16_t cmd) {
  TEST_ASSERT_LESS_THAN_INT(fake_txs_len, index);
  TEST_ASSERT_EQUAL_UINT8(target, fake_txs[index].target);
  TEST_ASSERT_EQUAL_HEX16(cmd, fake_txs[index].cmd);
}

// the wake, stop and offset preamble common to every config call
static void assert_config_preamble() {
  assert_tx(0, AL_SENSOR_HAL_SCD41, 0x36f6);
  assert_tx(1, AL_SENSOR_HAL_SCD41, 0x3f86);
  assert_tx(2, AL_SENSOR_HAL_SCD41, 0x241d);
  TEST_ASSERT_EQUAL_UINT16(1123, scd_offset);  // 3°C offset
}

/* Tests */

static void test_hal_crc() {
  hal_reset(false);

  // verify the unit's CRC against datasheet vectors
  uint8_t beef[] = {0xBE, 0xEF};
  uint8_t zero[] = {0x00, 0x00};
  uint8_t cond[] = {0x80, 0x00};
  TEST_ASSERT_EQUAL_HEX8(0x92, al_sensor_hal_crc(beef, 2));
  TEST_ASSERT_EQUAL_HEX8(0x81, al_sensor_hal_crc(zero, 2));
  TEST_ASSERT_EQUAL_HEX8(0xA2, al_sensor_hal_crc(cond, 2));
}

static void test_hal_config_normal() {
  hal_reset(false);

  // verify command sequence and state
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_NORMAL, 0, 0));
  assert_config_preamble();
  assert_tx(3, AL_SENSOR_HAL_SCD41, 0x21b1);  // start periodic
  assert_tx(4, AL_SENSOR_HAL_LPS22, 0x10);    // read back
  assert_tx(5, AL_SENSOR_HAL_LPS22, 0x10);    // write
  TEST_ASSERT_EQUAL_INT(6, fake_txs_len);
  TEST_ASSERT_EQUAL_HEX8(0x1A, lps_regs[0x10]);  // 1Hz, LPF+BDU on
  TEST_ASSERT_EQUAL_UINT32(530, fake_delayed);   // wake and stop delays
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_NORMAL, hal_state.mode);
  TEST_ASSERT_EQUAL_INT(0, hal_state.interval);
  TEST_ASSERT_EQUAL_INT(0, hal_state.duty);

  // verify a matching CTRL_REG1 skips the write
  fake_txs_len = 0;
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_NORMAL, 0, 0));
  assert_tx(4, AL_SENSOR_HAL_LPS22, 0x10);  // read back only
  TEST_ASSERT_EQUAL_INT(5, fake_txs_len);
}

static void test_hal_config_low_power() {
  hal_reset(false);

  // verify low power periodic measurement is started
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_LOW_POWER, 0, 0));
  assert_tx(3, AL_SENSOR_HAL_SCD41, 0x21ac);
  TEST_ASSERT_EQUAL_INT(6, fake_txs_len);
}

static void test_hal_config_manual_continuous() {
  hal_reset(false);

  // verify the SCD idles (no start, no power down) and the SGP heater is
  // turned on right away with the default compensation values
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_MANUAL, 55000, 0));
  assert_config_preamble();
  assert_tx(3, AL_SENSOR_HAL_SGP41, 0x2619);
  assert_tx(4, AL_SENSOR_HAL_LPS22, 0x10);
  TEST_ASSERT_EQUAL_INT(6, fake_txs_len);
  TEST_ASSERT_EQUAL_HEX16(0x8000, sgp_comp[0]);
  TEST_ASSERT_EQUAL_HEX16(0x6666, sgp_comp[1]);
}

static void test_hal_config_manual_duty() {
  hal_reset(false);

  // verify the SGP heater is turned off when entering duty-cycled manual mode
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_MANUAL, 55000, 20000));
  assert_tx(3, AL_SENSOR_HAL_SGP41, 0x3615);
  TEST_ASSERT_EQUAL_INT(6, fake_txs_len);
  TEST_ASSERT_EQUAL_INT(20000, hal_state.duty);
}

static void test_hal_config_manual_cycled() {
  hal_reset(false);

  // verify the SCD is powered down between shots on long intervals
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_MANUAL, AL_SENSOR_CYCLE_TIME, 0));
  assert_tx(3, AL_SENSOR_HAL_SCD41, 0x36e0);
  assert_tx(4, AL_SENSOR_HAL_SGP41, 0x2619);
  TEST_ASSERT_EQUAL_INT(7, fake_txs_len);
}

static void test_hal_config_sleep() {
  hal_reset(false);

  // verify the SCD is powered down, the heater turned off and the LPS stopped
  lps_regs[0x10] = 0x1A;  // running at 1Hz
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_SLEEP, 0, 0));
  assert_tx(3, AL_SENSOR_HAL_SCD41, 0x36e0);
  assert_tx(4, AL_SENSOR_HAL_SGP41, 0x3615);
  assert_tx(5, AL_SENSOR_HAL_LPS22, 0x10);  // read back
  assert_tx(6, AL_SENSOR_HAL_LPS22, 0x10);  // write
  TEST_ASSERT_EQUAL_INT(7, fake_txs_len);
  TEST_ASSERT_EQUAL_HEX8(0x00, lps_regs[0x10]);
}

static void test_hal_config_invalid() {
  hal_reset(false);

  // verify an unknown mode is rejected
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_ERR_MODE, al_sensor_hal_config((al_sensor_hal_mode_t)99, 0, 0));
}

static void test_hal_config_disabled() {
  hal_reset(false);

  // verify a negative duty turns the heater off even in normal mode
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_NORMAL, 0, -1));
  assert_tx(3, AL_SENSOR_HAL_SCD41, 0x21b1);
  assert_tx(4, AL_SENSOR_HAL_SGP41, 0x3615);

  // verify a read skips the SGP entirely and reports zero raw values
  scd_data[0] = 600;
  fake_txs_len = 0;
  al_sensor_hal_data_t data;
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_read(&data));
  TEST_ASSERT_EQUAL_UINT16(600, data.co2);
  TEST_ASSERT_EQUAL_UINT16(0, data.voc);
  TEST_ASSERT_EQUAL_UINT16(0, data.nox);
  for (int i = 0; i < fake_txs_len; i++) {
    TEST_ASSERT_NOT_EQUAL_UINT8(AL_SENSOR_HAL_SGP41, fake_txs[i].target);
  }
}

static void test_hal_error_flags() {
  // a failing transfer on a non-tolerated SCD command flags the SCD
  hal_reset(false);
  fail_target = AL_SENSOR_HAL_SCD41;
  fail_count = 3;  // wake and stop are tolerated, the offset write is not
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_ERR_TRANSFER | AL_SENSOR_HAL_ERR_SCD41,
                        al_sensor_hal_config(AL_SENSOR_HAL_NORMAL, 0, 0));

  // tolerated failures alone are swallowed
  hal_reset(false);
  fail_target = AL_SENSOR_HAL_SCD41;
  fail_count = 2;  // wake and stop only
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_NORMAL, 0, 0));

  // a failing SGP transfer flags the SGP
  hal_reset(false);
  fail_target = AL_SENSOR_HAL_SGP41;
  fail_count = 1;
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_ERR_TRANSFER | AL_SENSOR_HAL_ERR_SGP41, al_sensor_hal_heater_off());

  // a failing LPS transfer flags the LPS
  hal_reset(false);
  fail_target = AL_SENSOR_HAL_LPS22;
  fail_count = 1;
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_ERR_TRANSFER | AL_SENSOR_HAL_ERR_LPS22,
                        al_sensor_hal_config(AL_SENSOR_HAL_NORMAL, 0, 0));
}

static void test_hal_checksum() {
  hal_reset(false);
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_NORMAL, 0, 0));

  // a corrupted response checksum fails the read with the checksum error
  scd_data[0] = 600;
  corrupt_next = true;
  al_sensor_hal_data_t data;
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_ERR_CHECKSUM | AL_SENSOR_HAL_ERR_SCD41, al_sensor_hal_read(&data));

  // and makes the readiness check report not ready
  scd_ready_word = 0x0006;
  corrupt_next = true;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
}

static void test_hal_ready_normal() {
  hal_reset(false);
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_NORMAL, 0, 0));

  // not ready, ready, and only the lower 12 bits count
  scd_ready_word = 0x0000;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  scd_ready_word = 0x8000;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  scd_ready_word = 0x0006;
  TEST_ASSERT_TRUE(al_sensor_hal_ready());
}

static void test_hal_read_normal() {
  hal_reset(false);
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_NORMAL, 0, 0));

  // load measurement data
  scd_data[0] = 600;    // co2
  scd_data[1] = 25277;  // ~22.5°C
  scd_data[2] = 32768;  // 50% rH
  sgp_data[0] = 27000;
  sgp_data[1] = 15000;
  lps_regs[0x28] = 0x00;
  lps_regs[0x29] = 0x60;
  lps_regs[0x2A] = 0x40;  // 0x406000 = 1030hPa
  fake_epoch = 2000000;

  // verify the full frame decode
  al_sensor_hal_data_t data;
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_read(&data));
  TEST_ASSERT_EQUAL_UINT16(600, data.co2);
  TEST_ASSERT_EQUAL_UINT16(25277, data.tmp);
  TEST_ASSERT_EQUAL_UINT16(32768, data.hum);
  TEST_ASSERT_EQUAL_UINT16(27000, data.voc);
  TEST_ASSERT_EQUAL_UINT16(15000, data.nox);
  TEST_ASSERT_EQUAL_UINT32(0x406000, data.prs);
  TEST_ASSERT_EQUAL_INT64(2000000, data.epoch);

  // verify the SGP measurement was compensated with humidity then temperature
  TEST_ASSERT_EQUAL_HEX16(32768, sgp_comp[0]);
  TEST_ASSERT_EQUAL_HEX16(25277, sgp_comp[1]);

  // verify no deadline is scheduled outside manual mode
  TEST_ASSERT_EQUAL_INT64(0, hal_state.next);
}

static void test_hal_manual_flow() {
  hal_reset(false);
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_MANUAL, 55000, 0));
  int64_t t0 = fake_epoch;

  // the first check schedules the deadline without any traffic
  fake_txs_len = 0;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  TEST_ASSERT_EQUAL_INT(0, fake_txs_len);
  TEST_ASSERT_EQUAL_INT64(t0 + 55000, hal_state.next);

  // a deadline beyond the current interval is clamped
  hal_state.next = t0 + 200000;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  TEST_ASSERT_EQUAL_INT64(t0 + 55000, hal_state.next);

  // the shot is taken one measurement time before the deadline
  fake_epoch = t0 + 50000;
  fake_txs_len = 0;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  assert_tx(0, AL_SENSOR_HAL_SCD41, 0x36f6);
  assert_tx(1, AL_SENSOR_HAL_SCD41, 0x219d);
  TEST_ASSERT_EQUAL_INT(2, fake_txs_len);
  TEST_ASSERT_EQUAL_INT64(fake_epoch + 55000, hal_state.next);

  // early polls before the measurement completes are skipped
  fake_epoch = t0 + 52000;
  fake_txs_len = 0;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  TEST_ASSERT_EQUAL_INT(0, fake_txs_len);

  // at the due time the data-ready flag is polled
  fake_epoch = t0 + 55000;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  scd_ready_word = 0x0006;
  TEST_ASSERT_TRUE(al_sensor_hal_ready());

  // a read clears the shot and schedules the next deadline
  al_sensor_hal_data_t data;
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_read(&data));
  TEST_ASSERT_EQUAL_INT64(0, hal_state.shot);
  TEST_ASSERT_EQUAL_INT64(fake_epoch + 55000, hal_state.next);
}

static void test_hal_manual_duty_flow() {
  hal_reset(true);
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_MANUAL, 55000, 20000));
  int64_t t0 = fake_epoch;

  // schedule the deadline
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  TEST_ASSERT_EQUAL_INT64(t0 + 55000, hal_state.next);

  // conditioning starts at the beginning of the active window
  fake_epoch = t0 + 35000;
  fake_txs_len = 0;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  assert_tx(0, AL_SENSOR_HAL_SGP41, 0x2612);
  TEST_ASSERT_EQUAL_INT(1, fake_txs_len);
  TEST_ASSERT_EQUAL_INT64(fake_epoch, hal_state.heat);

  // raw measurements run at 1Hz once conditioning ends
  fake_epoch = t0 + 44000;
  fake_txs_len = 0;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  assert_tx(0, AL_SENSOR_HAL_SGP41, 0x2619);
  TEST_ASSERT_EQUAL_INT(1, fake_txs_len);
  fake_epoch = t0 + 44500;
  fake_txs_len = 0;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  TEST_ASSERT_EQUAL_INT(0, fake_txs_len);
  fake_epoch = t0 + 45000;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  assert_tx(0, AL_SENSOR_HAL_SGP41, 0x2619);

  // the reading after the shot completes turns the heater off again (the shot
  // is only taken at the deadline, as the window start bounded it)
  fake_epoch = t0 + 55000;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());  // takes the shot
  fake_epoch = t0 + 60000;
  scd_ready_word = 0x0006;
  TEST_ASSERT_TRUE(al_sensor_hal_ready());
  al_sensor_hal_data_t data;
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_read(&data));
  assert_tx(fake_txs_len - 2, AL_SENSOR_HAL_SGP41, 0x3615);
  TEST_ASSERT_EQUAL_INT64(0, hal_state.heat);
  TEST_ASSERT_EQUAL_INT64(0, hal_state.raw);
}

static void test_hal_manual_duty_runaway() {
  hal_reset(true);
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_MANUAL, 55000, 20000));

  // a heater on beyond the active window (e.g. after read errors) is turned
  // off, so the next cycle starts over cleanly
  hal_state.next = fake_epoch + 40000;
  hal_state.heat = fake_epoch - 20000 - AL_SENSOR_MAX_HEAT - 1;
  fake_txs_len = 0;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  assert_tx(0, AL_SENSOR_HAL_SGP41, 0x3615);
  TEST_ASSERT_EQUAL_INT64(0, hal_state.heat);
}

static void test_hal_manual_cycled_flow() {
  hal_reset(false);
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_MANUAL, AL_SENSOR_CYCLE_TIME, 0));
  int64_t t0 = fake_epoch;

  // schedule the deadline
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  TEST_ASSERT_EQUAL_INT64(t0 + AL_SENSOR_CYCLE_TIME, hal_state.next);

  // the shot is taken two measurement times early to fit the discarded
  // stabilization shot
  fake_epoch = t0 + AL_SENSOR_CYCLE_TIME - 2 * AL_SENSOR_MSR_TIME;
  fake_txs_len = 0;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  assert_tx(1, AL_SENSOR_HAL_SCD41, 0x219d);
  TEST_ASSERT_EQUAL_INT64(1, hal_state.shot);
  int64_t shot_at = fake_epoch;

  // the stabilization shot is read, discarded and the real measurement taken
  fake_epoch = shot_at + AL_SENSOR_MSR_TIME;
  scd_ready_word = 0x0006;
  fake_txs_len = 0;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  assert_tx(0, AL_SENSOR_HAL_SCD41, 0xe4b8);
  assert_tx(1, AL_SENSOR_HAL_SCD41, 0xec05);
  assert_tx(3, AL_SENSOR_HAL_SCD41, 0x219d);
  TEST_ASSERT_EQUAL_INT64(2, hal_state.shot);

  // the real measurement is read and the SCD powered down again
  fake_epoch = shot_at + 2 * AL_SENSOR_MSR_TIME;
  scd_ready_word = 0x0006;
  TEST_ASSERT_TRUE(al_sensor_hal_ready());
  fake_txs_len = 0;
  al_sensor_hal_data_t data;
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_read(&data));
  assert_tx(fake_txs_len - 1, AL_SENSOR_HAL_SCD41, 0x36e0);
  TEST_ASSERT_EQUAL_INT64(0, hal_state.shot);
}

static void test_hal_manual_stale_shot() {
  hal_reset(false);
  TEST_ASSERT_EQUAL_INT(AL_SENSOR_HAL_OK, al_sensor_hal_config(AL_SENSOR_HAL_MANUAL, AL_SENSOR_CYCLE_TIME, 0));

  // a shot that never completed is cleared after four measurement times and a
  // power-cycled SCD is powered down again
  hal_state.next = fake_epoch + AL_SENSOR_CYCLE_TIME - 4 * AL_SENSOR_MSR_TIME - 1;
  hal_state.shot = 2;
  fake_txs_len = 0;
  TEST_ASSERT_FALSE(al_sensor_hal_ready());
  assert_tx(0, AL_SENSOR_HAL_SCD41, 0x36e0);
  TEST_ASSERT_EQUAL_INT64(0, hal_state.shot);
}

/* Suite */

void suite_sensor_hal() {
  RUN_TEST(test_hal_crc);
  RUN_TEST(test_hal_config_normal);
  RUN_TEST(test_hal_config_low_power);
  RUN_TEST(test_hal_config_manual_continuous);
  RUN_TEST(test_hal_config_manual_duty);
  RUN_TEST(test_hal_config_manual_cycled);
  RUN_TEST(test_hal_config_sleep);
  RUN_TEST(test_hal_config_invalid);
  RUN_TEST(test_hal_config_disabled);
  RUN_TEST(test_hal_error_flags);
  RUN_TEST(test_hal_checksum);
  RUN_TEST(test_hal_ready_normal);
  RUN_TEST(test_hal_read_normal);
  RUN_TEST(test_hal_manual_flow);
  RUN_TEST(test_hal_manual_duty_flow);
  RUN_TEST(test_hal_manual_duty_runaway);
  RUN_TEST(test_hal_manual_cycled_flow);
  RUN_TEST(test_hal_manual_stale_shot);
}
