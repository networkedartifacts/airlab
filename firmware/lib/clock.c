#include <naos.h>
#include <sys/time.h>
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

  // write RTC fully
  al_clock_write(0x00, al_clock_memory.r0);
  al_clock_write(0x01, al_clock_memory.r1);
  al_clock_write(0x02, al_clock_memory.r2);
  al_clock_write(0x03, al_clock_memory.r3);
  al_clock_write(0x04, al_clock_memory.r4);
  al_clock_write(0x05, al_clock_memory.r5);
  al_clock_write(0x06, al_clock_memory.r6);
}

void al_clock_init() {
  // get clock
  al_clock_state_t state = al_clock_get();

  // get time
  time_t t = time(NULL);
  struct tm *cal = localtime(&t);

  // update time
  cal->tm_year = state.year - 1900;
  cal->tm_mon = state.month - 1;
  cal->tm_mday = state.day;
  cal->tm_hour = state.hours;
  cal->tm_min = state.minutes;
  cal->tm_sec = state.seconds;

  // set time
  t = mktime(cal);
  struct timeval tv = {.tv_sec = t};
  settimeofday(&tv, NULL);
}

void al_clock_update() {
  // get time
  time_t t = time(NULL);
  struct tm *cal = localtime(&t);

  // prepare state
  al_clock_state_t state = {
      .year = cal->tm_year + 1900,
      .month = cal->tm_mon + 1,
      .day = cal->tm_mday,
      .hours = cal->tm_hour,
      .minutes = cal->tm_min,
      .seconds = cal->tm_sec,
  };

  // set clock
  al_clock_set(state);
}
