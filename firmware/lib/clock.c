#include <naos.h>
#include <naos/sys.h>
#include <sys/time.h>
#include <time.h>
#include <esp_err.h>

#include <al/core.h>
#include <al/clock.h>

// Chip: BQ32000

#define AL_CLOCK_ADDR 0x68

typedef struct {
  uint8_t hours;   /* 0-23 */
  uint8_t minutes; /* 0-59 */
  uint8_t seconds; /* 0-59 */
  uint8_t weekday; /* 1-7 */
  uint8_t day;     /* 1-31 */
  uint8_t month;   /* 1-12 */
  uint16_t year;   /* 2000-2099 */
} al_clock_state_t;

static struct {
  union {
    struct {
      uint8_t seconds : 4;
      uint8_t ten_seconds : 3;
      uint8_t _stop : 1;
    };
    uint8_t r0;
  };
  union {
    struct {
      uint8_t minutes : 4;
      uint8_t ten_minutes : 3;
      uint8_t _osc_fail : 1;
    };
    uint8_t r1;
  };
  union {
    struct {
      uint8_t hours : 4;
      uint8_t ten_hours : 2;
      uint8_t century : 1;
      uint8_t _cent_en : 1;
    };
    uint8_t r2;
  };
  union {
    struct {
      uint8_t weekday : 3;
      uint8_t _reserved1 : 5;
    };
    uint8_t r3;
  };
  union {
    struct {
      uint8_t days : 4;
      uint8_t ten_days : 2;
      uint8_t _reserved2 : 2;
    };
    uint8_t r4;
  };
  union {
    struct {
      uint8_t months : 4;
      uint8_t ten_months : 1;
      uint8_t _reserved3 : 3;
    };
    uint8_t r5;
  };
  union {
    struct {
      uint8_t years : 4;
      uint8_t ten_years : 4;
    };
    uint8_t r6;
  };
} al_clock_memory;

static void al_clock_read(uint8_t reg, uint8_t *buf, size_t read) {
  // write and read device
  ESP_ERROR_CHECK(al_i2c_transfer(AL_CLOCK_ADDR, &reg, 1, buf, read, 1000));
}

static void al_clock_write(uint8_t reg, uint8_t val) {
  // write device
  uint8_t data[2] = {reg, val};
  ESP_ERROR_CHECK(al_i2c_transfer(AL_CLOCK_ADDR, data, 2, NULL, 0, 1000));
}

static al_clock_state_t al_clock_get() {
  // read RTC fully
  al_clock_read(0x00, (uint8_t *)&al_clock_memory, sizeof(al_clock_memory));

  // convert BCD to DEC
  uint8_t seconds = al_clock_memory.seconds + (al_clock_memory.ten_seconds * 10);
  uint8_t minutes = al_clock_memory.minutes + (al_clock_memory.ten_minutes * 10);
  uint8_t hours = al_clock_memory.hours + (al_clock_memory.ten_hours * 10);
  uint8_t weekday = al_clock_memory.weekday;
  uint8_t date = al_clock_memory.days + (al_clock_memory.ten_days * 10);
  uint8_t month = al_clock_memory.months + (al_clock_memory.ten_months * 10);
  uint8_t year = al_clock_memory.years + (al_clock_memory.ten_years * 10);

  // handle overflow
  if (seconds >= 60) {
    seconds = 30;
  }
  if (minutes >= 60) {
    minutes = 30;
  }
  if (hours >= 24) {
    hours = 12;
  }
  if (weekday >= 7) {
    weekday = 3;
  }
  if (date >= 32) {
    date = 15;
  }
  if (month >= 13) {
    month = 6;
  }
  if (year >= 100) {
    year = 24;
  }

  // log
  naos_log("al-clk: get %02d:%02d:%02d %02d/%02d/%02d", hours, minutes, seconds, date, month, year);

  return (al_clock_state_t){
      .hours = hours,
      .minutes = minutes,
      .seconds = seconds,
      .weekday = weekday,
      .day = date,
      .month = month,
      .year = 2000 + year,
  };
}

static void al_clock_set(al_clock_state_t state) {
  // trim years
  state.year = state.year % 100;

  // log
  naos_log("al-clk: set %02d:%02d:%02d %02d/%02d/%02d", state.hours, state.minutes, state.seconds, state.day,
           state.month, state.year);

  // convert DEC to BCD
  al_clock_memory.seconds = state.seconds % 10;
  al_clock_memory.ten_seconds = state.seconds / 10;
  al_clock_memory.minutes = state.minutes % 10;
  al_clock_memory.ten_minutes = state.minutes / 10;
  al_clock_memory.hours = state.hours % 10;
  al_clock_memory.ten_hours = state.hours / 10;
  al_clock_memory.weekday = state.weekday;
  al_clock_memory.days = state.day % 10;
  al_clock_memory.ten_days = state.day / 10;
  al_clock_memory.months = state.month % 10;
  al_clock_memory.ten_months = state.month / 10;
  al_clock_memory.years = state.year % 10;
  al_clock_memory.ten_years = state.year / 10;

  // clear STOP and OF bits
  al_clock_memory._stop = 0;
  al_clock_memory._osc_fail = 0;

  // set STOP=1 to halt oscillator divider chain
  al_clock_write(0x00, al_clock_memory.r0 | 0x80);

  // write all registers in reverse, R0 last clears STOP and restarts divider
  al_clock_write(0x06, al_clock_memory.r6);
  al_clock_write(0x05, al_clock_memory.r5);
  al_clock_write(0x04, al_clock_memory.r4);
  al_clock_write(0x03, al_clock_memory.r3);
  al_clock_write(0x02, al_clock_memory.r2);
  al_clock_write(0x01, al_clock_memory.r1);
  al_clock_write(0x00, al_clock_memory.r0);
}

static time_t al_clock_timegm(int year, int mon, int day, int hour, int min, int sec) {
  // treat Jan/Feb as months 13/14 of the previous year so the leap day comes last
  mon -= 2;
  if (mon <= 0) {
    mon += 12;
    year -= 1;
  }

  // count leap days and month-offset days
  int64_t leap_days = year / 4 - year / 100 + year / 400;
  int64_t month_days = 367LL * mon / 12;

  // compute days since 1970-01-01
  int64_t days = leap_days + month_days + day + (int64_t)year * 365 - 719499;

  // combine days and time-of-day into seconds
  int64_t seconds = days * 86400 + (int64_t)hour * 3600 + (int64_t)min * 60 + sec;

  return (time_t)seconds;
}

static int64_t al_clock_sync_wall_ms = 0;
static int64_t al_clock_sync_mono_ms = 0;

static void al_clock_sync_mark(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  al_clock_sync_wall_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
  al_clock_sync_mono_ms = naos_millis();
}

static void al_clock_sync_check(void) {
  // read current wall and monotonic clocks
  struct timeval tv;
  gettimeofday(&tv, NULL);
  int64_t now_wall = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
  int64_t now_mono = naos_millis();

  // detect wall-clock step: |actual - expected| > 2s
  int64_t expected = al_clock_sync_wall_ms + (now_mono - al_clock_sync_mono_ms);
  int64_t delta = now_wall - expected;
  if (delta < -2000 || delta > 2000) {
    naos_log("al-clk: clock step detected (delta=%lldms), mirroring to RTC", delta);
    al_clock_update();
  }

  // update cache
  al_clock_sync_wall_ms = now_wall;
  al_clock_sync_mono_ms = now_mono;
}

void al_clock_init(bool reset) {
  // get clock
  al_clock_state_t state = al_clock_get();

  // check STOP and OF flags
  if (al_clock_memory._stop || al_clock_memory._osc_fail) {
    if (!reset) {
      naos_log("al-clk: warning: STOP=%d OF=%d, clearing flags", al_clock_memory._stop, al_clock_memory._osc_fail);
    }
    al_clock_memory._stop = 0;
    al_clock_memory._osc_fail = 0;
    al_clock_write(0x00, al_clock_memory.r0);
    al_clock_write(0x01, al_clock_memory.r1);
  }

  // seed system time from RTC (stored as UTC)
  time_t t = al_clock_timegm(state.year, state.month, state.day, state.hours, state.minutes, state.seconds);
  struct timeval tv = {.tv_sec = t};
  settimeofday(&tv, NULL);

  // start background sync
  al_clock_sync_mark();
  naos_repeat_defer("al-clk-sync", 20000, al_clock_sync_check);
}

void al_clock_update() {
  // get UTC time
  time_t t = time(NULL);
  struct tm cal;
  gmtime_r(&t, &cal);

  // prepare state
  al_clock_state_t state = {
      .year = cal.tm_year + 1900,
      .month = cal.tm_mon + 1,
      .day = cal.tm_mday,
      .hours = cal.tm_hour,
      .minutes = cal.tm_min,
      .seconds = cal.tm_sec,
  };

  // set clock
  al_clock_set(state);

  // refresh sync cache
  al_clock_sync_mark();
}

void al_clock_set_calibration(int8_t ppm) {
  // convert ppm to register value
  // S=0 (slow down): each step = +2 ppm => N = ppm / 2
  // S=1 (speed up): each step = -4 ppm => N = abs(ppm) / 4
  uint8_t val;
  if (ppm > 0) {
    // speed up: S=1
    uint8_t mag = ppm / 4;
    if (mag > 31) {
      mag = 31;
    }
    val = mag | 0x20;
  } else if (ppm < 0) {
    // slow down: S=0
    uint8_t mag = (-ppm) / 2;
    if (mag > 31) {
      mag = 31;
    }
    val = mag;
  } else {
    val = 0;
  }

  // write CAL_CFG1 register (0x07)
  al_clock_write(0x07, val);

  // log
  naos_log("al-clk: calibration set to %d ppm (reg=0x%02X)", ppm, val);
}
