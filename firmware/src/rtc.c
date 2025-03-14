#include <naos.h>
#include <driver/i2c.h>

#include "rtc.h"
#include "sys.h"

#define RTC_ADDR 0x68
#define RTC_DEBUG false

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
} rtc_bq32000;

static void rtc_read(uint8_t reg, uint8_t *buf, size_t read) {
  // write and read device
  ESP_ERROR_CHECK(i2c_master_write_read_device(I2C_NUM_0, RTC_ADDR, &reg, 1, buf, read, 1000));
}

static void rtc_write(uint8_t reg, uint8_t val) {
  // write device
  uint8_t data[2] = {reg, val};
  ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, RTC_ADDR, data, 2, 1000));
}

rtc_state_t rtc_get() {
  // read RTC fully
  rtc_read(0x00, (uint8_t *)&rtc_bq32000, sizeof(rtc_bq32000));

  // convert BCD to DEC
  uint8_t seconds = rtc_bq32000.seconds + (rtc_bq32000.ten_seconds * 10);
  uint8_t minutes = rtc_bq32000.minutes + (rtc_bq32000.ten_minutes * 10);
  uint8_t hours = rtc_bq32000.hours + (rtc_bq32000.ten_hours * 10);
  uint8_t weekday = rtc_bq32000.weekday;
  uint8_t date = rtc_bq32000.days + (rtc_bq32000.ten_days * 10);
  uint8_t month = rtc_bq32000.months + (rtc_bq32000.ten_months * 10);
  uint8_t year = rtc_bq32000.years + (rtc_bq32000.ten_years * 10);

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

  // log RTC state
  if (RTC_DEBUG) {
    naos_log("rtc: get %02d:%02d:%02d %02d/%02d/%02d", hours, minutes, seconds, date, month, year);
  }

  return (rtc_state_t){
      .hours = hours,
      .minutes = minutes,
      .seconds = seconds,
      .weekday = weekday,
      .day = date,
      .month = month,
      .year = 2000 + year,
  };
}

void rtc_set(rtc_state_t state) {
  // trim years
  state.year = state.year % 100;

  // log RTC state
  if (RTC_DEBUG) {
    naos_log("rtc: set %02d:%02d:%02d %02d/%02d/%02d", state.hours, state.minutes, state.seconds, state.day,
             state.month, state.year);
  }

  // convert DEC to BCD
  rtc_bq32000.seconds = state.seconds % 10;
  rtc_bq32000.ten_seconds = state.seconds / 10;
  rtc_bq32000.minutes = state.minutes % 10;
  rtc_bq32000.ten_minutes = state.minutes / 10;
  rtc_bq32000.hours = state.hours % 10;
  rtc_bq32000.ten_hours = state.hours / 10;
  rtc_bq32000.weekday = state.weekday;
  rtc_bq32000.days = state.day % 10;
  rtc_bq32000.ten_days = state.day / 10;
  rtc_bq32000.months = state.month % 10;
  rtc_bq32000.ten_months = state.month / 10;
  rtc_bq32000.years = state.year % 10;
  rtc_bq32000.ten_years = state.year / 10;

  // write RTC fully
  rtc_write(0x00, rtc_bq32000.r0);
  rtc_write(0x01, rtc_bq32000.r1);
  rtc_write(0x02, rtc_bq32000.r2);
  rtc_write(0x03, rtc_bq32000.r3);
  rtc_write(0x04, rtc_bq32000.r4);
  rtc_write(0x05, rtc_bq32000.r5);
  rtc_write(0x06, rtc_bq32000.r6);
}
