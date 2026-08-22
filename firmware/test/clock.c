#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include <unity.h>

#include <naos/sys.h>

/* Virtual Clock */

// wall and monotonic clocks under test control, interposed into the included
// units below via function-like macros
static int64_t fake_wall_us = 0;
static int64_t fake_mono_ms = 0;

static time_t fake_time(time_t *out) {
  time_t sec = (time_t)(fake_wall_us / 1000000);
  if (out != NULL) {
    *out = sec;
  }
  return sec;
}

static int fake_gettimeofday(struct timeval *tv, void *tz) {
  (void)tz;
  tv->tv_sec = (time_t)(fake_wall_us / 1000000);
  tv->tv_usec = (suseconds_t)(fake_wall_us % 1000000);
  return 0;
}

static int fake_settimeofday(const struct timeval *tv, const void *tz) {
  (void)tz;
  fake_wall_us = (int64_t)tv->tv_sec * 1000000 + tv->tv_usec;
  return 0;
}

int64_t naos_millis() { return fake_mono_ms; }

naos_timer_t naos_repeat_defer(const char *name, uint32_t period_ms, naos_func_t func) {
  (void)name;
  (void)period_ms;
  (void)func;
  return NULL;
}

// include the units directly to access their static state, with the wall
// clock functions redirected to the fakes above
#define time(t) fake_time(t)
#define gettimeofday(tv, tz) fake_gettimeofday(tv, tz)
#define settimeofday(tv, tz) fake_settimeofday(tv, tz)
#include "../lib/clock.c"
#include "../lib/clock_tools.c"
#undef time
#undef gettimeofday
#undef settimeofday

/* Fake RTC */

// a minimal BQ32000 model: a register file served over the I2C shim, with
// knobs to fail transfers and to roll a second over between reads
static uint8_t rtc_regs[8];
static bool rtc_offline = false;
static int rtc_fail_reads = 0;
static int rtc_bump_in = 0;
static int rtc_reads = 0;
static int rtc_writes = 0;

static uint8_t bcd(uint8_t value) { return (uint8_t)(((value / 10) << 4) | (value % 10)); }

static uint8_t unbcd(uint8_t value) { return (uint8_t)((value >> 4) * 10 + (value & 0x0F)); }

esp_err_t al_i2c_transfer(uint8_t addr, uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len, int timeout,
                          bool retry) {
  (void)timeout;
  (void)retry;
  TEST_ASSERT_EQUAL_UINT8(AL_CLOCK_ADDR, addr);

  // handle register read
  if (rx_len > 0) {
    rtc_reads++;
    if (rtc_offline || rtc_fail_reads > 0) {
      if (rtc_fail_reads > 0) {
        rtc_fail_reads--;
      }
      return ESP_FAIL;
    }
    TEST_ASSERT_EQUAL_size_t(1, tx_len);
    TEST_ASSERT(tx[0] + rx_len <= sizeof(rtc_regs));
    memcpy(rx, &rtc_regs[tx[0]], rx_len);

    // simulate a second rollover straddling consecutive reads
    if (rtc_bump_in > 0 && --rtc_bump_in == 0) {
      rtc_regs[0] = (uint8_t)((rtc_regs[0] & 0x80) | bcd((uint8_t)((unbcd(rtc_regs[0] & 0x7F) + 1) % 60)));
    }

    return ESP_OK;
  }

  // handle register write
  rtc_writes++;
  if (rtc_offline) {
    return ESP_FAIL;
  }
  TEST_ASSERT_EQUAL_size_t(2, tx_len);
  TEST_ASSERT(tx[0] < sizeof(rtc_regs));
  rtc_regs[tx[0]] = tx[1];

  return ESP_OK;
}

/* Helpers */

static void clock_reset() {
  // reset fakes
  fake_wall_us = 0;
  fake_mono_ms = 0;
  memset(rtc_regs, 0, sizeof(rtc_regs));
  rtc_offline = false;
  rtc_fail_reads = 0;
  rtc_bump_in = 0;
  rtc_reads = 0;
  rtc_writes = 0;

  // reset unit state
  memset(&al_clock_memory, 0, sizeof(al_clock_memory));
  al_clock_sync_wall_ms = 0;
  al_clock_sync_mono_ms = 0;

  // pin timezone, tools tests override
  setenv("TZ", "UTC0", 1);
  tzset();
}

static time_t mkutc(int year, int mon, int day, int hour, int min, int sec) {
  return al_clock_timegm(year, mon, day, hour, min, sec);
}

static void rtc_load(time_t t) {
  struct tm cal;
  gmtime_r(&t, &cal);
  rtc_regs[0] = bcd((uint8_t)cal.tm_sec);
  rtc_regs[1] = bcd((uint8_t)cal.tm_min);
  rtc_regs[2] = bcd((uint8_t)cal.tm_hour);
  rtc_regs[3] = (uint8_t)(cal.tm_wday + 1);
  rtc_regs[4] = bcd((uint8_t)cal.tm_mday);
  rtc_regs[5] = bcd((uint8_t)(cal.tm_mon + 1));
  rtc_regs[6] = bcd((uint8_t)(cal.tm_year % 100));
}

static int64_t rtc_epoch() {
  return al_clock_timegm(2000 + unbcd(rtc_regs[6]), unbcd(rtc_regs[5] & 0x1F), unbcd(rtc_regs[4] & 0x3F),
                         unbcd(rtc_regs[2] & 0x3F), unbcd(rtc_regs[1] & 0x7F), unbcd(rtc_regs[0] & 0x7F));
}

static void set_wall(time_t sec, int64_t usec) { fake_wall_us = (int64_t)sec * 1000000 + usec; }

// non-static, as other suites use it to steer the wall clock
void test_clock_set_epoch(int64_t epoch) { fake_wall_us = epoch * 1000; }

static int64_t wall_sec() { return fake_wall_us / 1000000; }

static void advance(int64_t ms) {
  fake_wall_us += ms * 1000;
  fake_mono_ms += ms;
}

#define TZ_EU "CET-1CEST,M3.5.0,M10.5.0/3"

/* Tests */

static void test_clock_timegm() {
  clock_reset();

  // verify known epochs
  TEST_ASSERT_EQUAL_INT64(0, al_clock_timegm(1970, 1, 1, 0, 0, 0));
  TEST_ASSERT_EQUAL_INT64(951782400, al_clock_timegm(2000, 2, 29, 0, 0, 0));
  TEST_ASSERT_EQUAL_INT64(1787356800, al_clock_timegm(2026, 8, 22, 0, 0, 0));

  // sweep the supported range against the host conversion
  for (time_t t = mkutc(2000, 1, 1, 0, 0, 0); t <= mkutc(2099, 12, 31, 23, 59, 59); t += 86400 * 13 + 4321) {
    struct tm cal;
    gmtime_r(&t, &cal);
    TEST_ASSERT_EQUAL_INT64(
        t, al_clock_timegm(cal.tm_year + 1900, cal.tm_mon + 1, cal.tm_mday, cal.tm_hour, cal.tm_min, cal.tm_sec));
  }
}

static void test_clock_set_read() {
  clock_reset();

  // set state and verify BCD registers with STOP cleared again
  al_clock_set((al_clock_state_t){
      .hours = 23, .minutes = 59, .seconds = 41, .weekday = 6, .day = 22, .month = 8, .year = 2026});
  TEST_ASSERT_EQUAL_INT(8, rtc_writes);
  TEST_ASSERT_EQUAL_HEX8(0x41, rtc_regs[0]);
  TEST_ASSERT_EQUAL_HEX8(0x59, rtc_regs[1]);
  TEST_ASSERT_EQUAL_HEX8(0x23, rtc_regs[2]);
  TEST_ASSERT_EQUAL_HEX8(0x06, rtc_regs[3]);
  TEST_ASSERT_EQUAL_HEX8(0x22, rtc_regs[4]);
  TEST_ASSERT_EQUAL_HEX8(0x08, rtc_regs[5]);
  TEST_ASSERT_EQUAL_HEX8(0x26, rtc_regs[6]);

  // read back state
  al_clock_state_t state;
  TEST_ASSERT_TRUE(al_clock_read_state(&state));
  TEST_ASSERT_EQUAL_INT(23, state.hours);
  TEST_ASSERT_EQUAL_INT(59, state.minutes);
  TEST_ASSERT_EQUAL_INT(41, state.seconds);
  TEST_ASSERT_EQUAL_INT(6, state.weekday);
  TEST_ASSERT_EQUAL_INT(22, state.day);
  TEST_ASSERT_EQUAL_INT(8, state.month);
  TEST_ASSERT_EQUAL_INT(2026, state.year);
}

static void test_clock_decode_invalid() {
  // verify out-of-range fields are rejected after all read attempts
  struct {
    int reg;
    uint8_t val;
  } cases[] = {{0, 0x61}, {1, 0x60}, {2, 0x24}, {4, 0x00}, {4, 0x32}, {5, 0x00}, {5, 0x13}};
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    clock_reset();
    rtc_load(mkutc(2026, 8, 22, 12, 0, 0));
    rtc_regs[cases[i].reg] = cases[i].val;
    al_clock_state_t state;
    TEST_ASSERT_FALSE(al_clock_read_state(&state));
    TEST_ASSERT_EQUAL_INT(6, rtc_reads);
  }

  // verify an invalid weekday is clamped rather than rejected
  clock_reset();
  rtc_load(mkutc(2026, 8, 22, 12, 0, 0));
  rtc_regs[3] = 0;
  al_clock_state_t state;
  TEST_ASSERT_TRUE(al_clock_read_state(&state));
  TEST_ASSERT_EQUAL_INT(1, state.weekday);
}

static void test_clock_read_rollover() {
  clock_reset();

  // a rollover between the first two reads must be resolved by a third
  rtc_load(mkutc(2026, 8, 22, 12, 0, 41));
  rtc_bump_in = 1;
  al_clock_state_t state;
  TEST_ASSERT_TRUE(al_clock_read_state(&state));
  TEST_ASSERT_EQUAL_INT(42, state.seconds);
  TEST_ASSERT_EQUAL_INT(3, rtc_reads);
}

static void test_clock_read_stale() {
  clock_reset();

  // fill the register memory with an old time
  rtc_load(mkutc(2026, 8, 22, 12, 10, 0));
  al_clock_state_t state;
  TEST_ASSERT_TRUE(al_clock_read_state(&state));

  // a failed read leaves the memory stale, which must not count as agreement
  // with the following successful read
  rtc_load(mkutc(2026, 8, 22, 12, 20, 0));
  rtc_reads = 0;
  rtc_fail_reads = 1;
  TEST_ASSERT_TRUE(al_clock_read_state(&state));
  TEST_ASSERT_EQUAL_INT(20, state.minutes);
  TEST_ASSERT_EQUAL_INT(3, rtc_reads);
}

static void test_clock_read_offline() {
  clock_reset();

  // an unreadable RTC falls back to the system time without any writes
  rtc_offline = true;
  set_wall(mkutc(2026, 8, 22, 12, 34, 56), 0);
  al_clock_state_t state;
  TEST_ASSERT_FALSE(al_clock_get(&state));
  TEST_ASSERT_EQUAL_INT(12, state.hours);
  TEST_ASSERT_EQUAL_INT(34, state.minutes);
  TEST_ASSERT_EQUAL_INT(56, state.seconds);
  TEST_ASSERT_EQUAL_INT(22, state.day);
  TEST_ASSERT_EQUAL_INT(8, state.month);
  TEST_ASSERT_EQUAL_INT(2026, state.year);
  TEST_ASSERT_EQUAL_INT(0, rtc_writes);
}

static void test_clock_init_cold_rtc_valid() {
  clock_reset();

  // cold boot: a valid post-build RTC seeds the system time untouched
  time_t t = al_clock_build_time() + 3600;
  rtc_load(t);
  al_clock_init(true);
  TEST_ASSERT_EQUAL_INT64(t, wall_sec());
  TEST_ASSERT_EQUAL_INT(0, rtc_writes);
}

static void test_clock_init_cold_rtc_prebuild() {
  clock_reset();

  // cold boot: a pre-build RTC raises the system time to the build time and
  // is healed with it
  time_t bd = al_clock_build_time() - 86400;
  rtc_load(bd - 5000);
  al_clock_init(true);
  TEST_ASSERT_EQUAL_INT64(bd, wall_sec());
  TEST_ASSERT_EQUAL_INT64(bd, rtc_epoch());
}

static void test_clock_init_cold_rtc_dead() {
  clock_reset();

  // cold boot: an unreadable RTC still raises the system time to the build
  // time, and no garbage or flag write-back reaches the chip
  rtc_offline = true;
  al_clock_init(true);
  TEST_ASSERT_EQUAL_INT64(al_clock_build_time() - 86400, wall_sec());
  TEST_ASSERT_EQUAL_INT(8, rtc_writes);  // only the attempted heal, no flag writes
  uint8_t zero[sizeof(rtc_regs)] = {0};
  TEST_ASSERT_EQUAL_MEMORY(zero, rtc_regs, sizeof(rtc_regs));
}

static void test_clock_init_wake_rtc_dead() {
  clock_reset();

  // wake: an unreadable RTC keeps the surviving system time (sub-second
  // truncated by the reseed)
  rtc_offline = true;
  time_t t = al_clock_build_time() + 7 * 86400;
  set_wall(t, 500000);
  al_clock_init(false);
  TEST_ASSERT_EQUAL_INT64((int64_t)t * 1000000, fake_wall_us);
}

static void test_clock_init_wake_rtc_behind() {
  clock_reset();

  // wake: an RTC more than 120s behind the surviving system time is healed
  time_t t = al_clock_build_time() + 10 * 86400;
  set_wall(t, 0);
  rtc_load(t - 300);
  al_clock_init(false);
  TEST_ASSERT_EQUAL_INT64(t, wall_sec());
  TEST_ASSERT_EQUAL_INT64(t, rtc_epoch());
}

static void test_clock_init_wake_rtc_tolerated() {
  clock_reset();

  // wake: within the 120s tolerance the RTC wins over the system time, which
  // covers the internal counter's drift over a deep sleep
  time_t t = al_clock_build_time() + 10 * 86400;
  set_wall(t, 0);
  rtc_load(t - 60);
  al_clock_init(false);
  TEST_ASSERT_EQUAL_INT64(t - 60, wall_sec());
  TEST_ASSERT_EQUAL_INT(0, rtc_writes);
}

static void test_clock_init_wake_rtc_ahead() {
  clock_reset();

  // wake: an RTC ahead of the system time is trusted without bound, as the
  // system time may have fallen behind or reset (documents the asymmetry to
  // the backwards guard)
  time_t t = al_clock_build_time() + 10 * 86400;
  set_wall(t, 0);
  rtc_load(t + 3600);
  al_clock_init(false);
  TEST_ASSERT_EQUAL_INT64(t + 3600, wall_sec());
  TEST_ASSERT_EQUAL_INT(0, rtc_writes);
}

static void test_clock_init_flags() {
  clock_reset();

  // wake: STOP and OF flags are cleared with two register writes, while the
  // stored time is still trusted for the seed
  time_t t = al_clock_build_time() + 3600;
  rtc_load(t);
  rtc_regs[0] |= 0x80;  // STOP
  rtc_regs[1] |= 0x80;  // OF
  al_clock_init(false);
  TEST_ASSERT_EQUAL_HEX8(0x00, rtc_regs[0] & 0x80);
  TEST_ASSERT_EQUAL_HEX8(0x00, rtc_regs[1] & 0x80);
  TEST_ASSERT_EQUAL_INT(2, rtc_writes);
  TEST_ASSERT_EQUAL_INT64(t, wall_sec());
}

static void test_clock_sync() {
  clock_reset();

  // initialize with matching clocks
  time_t t = al_clock_build_time() + 30 * 86400;
  set_wall(t, 0);
  rtc_load(t);
  al_clock_init(false);
  rtc_writes = 0;

  // plain passage of time is not a step
  advance(20000);
  al_clock_sync_check();
  TEST_ASSERT_EQUAL_INT(0, rtc_writes);

  // a step within the 2s tolerance is absorbed
  fake_wall_us += 1500 * 1000;
  advance(20000);
  al_clock_sync_check();
  TEST_ASSERT_EQUAL_INT(0, rtc_writes);

  // a larger step is mirrored to the RTC
  fake_wall_us += 10 * 1000000;
  al_clock_sync_check();
  TEST_ASSERT_EQUAL_INT(8, rtc_writes);
  TEST_ASSERT_EQUAL_INT64(wall_sec(), rtc_epoch());

  // and the cache is refreshed afterwards
  rtc_writes = 0;
  advance(20000);
  al_clock_sync_check();
  TEST_ASSERT_EQUAL_INT(0, rtc_writes);
}

static void test_clock_flush() {
  clock_reset();

  // initialize with matching clocks
  time_t t = al_clock_build_time() + 30 * 86400;
  set_wall(t, 0);
  rtc_load(t);
  al_clock_init(false);
  rtc_writes = 0;

  // a pending backward step is mirrored by a flush, e.g. before deep sleep
  advance(5000);
  fake_wall_us -= 30 * 1000000;
  al_clock_flush();
  TEST_ASSERT_EQUAL_INT(8, rtc_writes);
  TEST_ASSERT_EQUAL_INT64(wall_sec(), rtc_epoch());
}

static void test_clock_verify() {
  clock_reset();

  // verify reads the RTC without touching the system time or the chip
  rtc_load(mkutc(2026, 8, 22, 12, 0, 0));
  set_wall(mkutc(2030, 1, 1, 0, 0, 0), 0);
  uint16_t year = 0;
  TEST_ASSERT_TRUE(al_clock_verify(&year));
  TEST_ASSERT_EQUAL_UINT16(2026, year);
  TEST_ASSERT_EQUAL_INT64(mkutc(2030, 1, 1, 0, 0, 0), wall_sec());
  TEST_ASSERT_EQUAL_INT(0, rtc_writes);

  // and fails cleanly on an unreadable RTC
  rtc_offline = true;
  TEST_ASSERT_FALSE(al_clock_verify(&year));
}

static void test_clock_calibration() {
  clock_reset();

  // verify ppm mapping into CAL_CFG1 with the OUT bit always set
  struct {
    int8_t ppm;
    uint8_t reg;
  } cases[] = {{0, 0x80}, {4, 0xA1}, {126, 0xBF}, {127, 0xBF}, {-2, 0x81}, {-63, 0x9F}, {-128, 0x9F}};
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    al_clock_set_calibration(cases[i].ppm);
    TEST_ASSERT_EQUAL_HEX8(cases[i].reg, rtc_regs[7]);
  }
}

static void test_clock_tools_local() {
  clock_reset();
  setenv("TZ", TZ_EU, 1);
  tzset();

  // winter: UTC+1
  set_wall(mkutc(2026, 1, 15, 11, 0, 0), 0);
  uint16_t year, month, day, hour, minute, second;
  al_clock_get_time(&hour, &minute, &second);
  al_clock_get_date(&year, &month, &day);
  TEST_ASSERT_EQUAL_UINT16(12, hour);
  TEST_ASSERT_EQUAL_UINT16(2026, year);
  TEST_ASSERT_EQUAL_UINT16(1, month);
  TEST_ASSERT_EQUAL_UINT16(15, day);

  // summer: UTC+2
  set_wall(mkutc(2026, 7, 15, 10, 0, 0), 0);
  al_clock_get_time(&hour, &minute, &second);
  TEST_ASSERT_EQUAL_UINT16(12, hour);
}

static void test_clock_tools_set_time() {
  clock_reset();
  setenv("TZ", TZ_EU, 1);
  tzset();

  // a set local time is converted using the DST offset of the current date
  // and mirrored to the RTC as UTC
  set_wall(mkutc(2026, 7, 15, 9, 0, 0), 0);  // 11:00 local
  al_clock_set_time(14, 30, 0);
  TEST_ASSERT_EQUAL_INT64(mkutc(2026, 7, 15, 12, 30, 0), wall_sec());
  TEST_ASSERT_EQUAL_INT64(wall_sec(), rtc_epoch());
}

static void test_clock_tools_set_date() {
  clock_reset();
  setenv("TZ", TZ_EU, 1);
  tzset();

  // a set date re-derives the DST offset, keeping the local time of day
  set_wall(mkutc(2026, 1, 15, 11, 0, 0), 0);  // 12:00 local, winter
  al_clock_set_date(2026, 7, 15);
  TEST_ASSERT_EQUAL_INT64(mkutc(2026, 7, 15, 10, 0, 0), wall_sec());
  uint16_t hour, minute, second;
  al_clock_get_time(&hour, &minute, &second);
  TEST_ASSERT_EQUAL_UINT16(12, hour);
  TEST_ASSERT_EQUAL_INT64(wall_sec(), rtc_epoch());
}

static void test_clock_tools_epoch() {
  clock_reset();

  // epoch set/get round-trips with milliseconds and mirrors to the RTC
  al_clock_set_epoch(1787356800123);
  TEST_ASSERT_EQUAL_INT64(1787356800123, al_clock_get_epoch());
  TEST_ASSERT_EQUAL_INT64(1787356800, rtc_epoch());

  // epoch conversions honor the timezone
  setenv("TZ", TZ_EU, 1);
  tzset();
  int64_t ts = (int64_t)mkutc(2026, 1, 15, 11, 0, 0) * 1000;
  uint16_t year, month, day, hour, minute, second;
  al_clock_epoch_date(ts, &year, &month, &day);
  al_clock_epoch_time(ts, &hour, &minute, &second);
  TEST_ASSERT_EQUAL_UINT16(2026, year);
  TEST_ASSERT_EQUAL_UINT16(1, month);
  TEST_ASSERT_EQUAL_UINT16(15, day);
  TEST_ASSERT_EQUAL_UINT16(12, hour);
}

/* Suite */

void suite_clock() {
  RUN_TEST(test_clock_timegm);
  RUN_TEST(test_clock_set_read);
  RUN_TEST(test_clock_decode_invalid);
  RUN_TEST(test_clock_read_rollover);
  RUN_TEST(test_clock_read_stale);
  RUN_TEST(test_clock_read_offline);
  RUN_TEST(test_clock_init_cold_rtc_valid);
  RUN_TEST(test_clock_init_cold_rtc_prebuild);
  RUN_TEST(test_clock_init_cold_rtc_dead);
  RUN_TEST(test_clock_init_wake_rtc_dead);
  RUN_TEST(test_clock_init_wake_rtc_behind);
  RUN_TEST(test_clock_init_wake_rtc_tolerated);
  RUN_TEST(test_clock_init_wake_rtc_ahead);
  RUN_TEST(test_clock_init_flags);
  RUN_TEST(test_clock_sync);
  RUN_TEST(test_clock_flush);
  RUN_TEST(test_clock_verify);
  RUN_TEST(test_clock_calibration);
  RUN_TEST(test_clock_tools_local);
  RUN_TEST(test_clock_tools_set_time);
  RUN_TEST(test_clock_tools_set_date);
  RUN_TEST(test_clock_tools_epoch);
}
