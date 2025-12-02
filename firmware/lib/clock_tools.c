#include <time.h>
#include <sys/time.h>

#include <al/clock.h>

#include "internal.h"

void al_clock_get_date(uint16_t *year, uint16_t *month, uint16_t *day) {
  // get time as calendar
  time_t t = time(NULL);
  struct tm *cal = localtime(&t);

  // set day, month and year
  *year = cal->tm_year + 1900;
  *month = cal->tm_mon + 1;
  *day = cal->tm_mday;
}

void al_clock_set_date(uint16_t year, uint16_t month, uint16_t day) {
  // get time as calendar
  time_t t = time(NULL);
  struct tm *cal = localtime(&t);

  // set day, month and year
  cal->tm_year = year - 1900;
  cal->tm_mon = month - 1;
  cal->tm_mday = day;

  // make time
  t = mktime(cal);

  // set time
  struct timeval tv = {.tv_sec = t};
  settimeofday(&tv, NULL);

  // update clock
  al_clock_update();
}

void al_clock_get_time(uint16_t *hour, uint16_t *minute, uint16_t *seconds) {
  // get time as calendar
  time_t t = time(NULL);
  struct tm *cal = localtime(&t);

  // set hour, minute and seconds
  *hour = cal->tm_hour;
  *minute = cal->tm_min;
  *seconds = cal->tm_sec;
}

void al_clock_set_time(uint16_t hour, uint16_t minute, uint16_t seconds) {
  // get time as calendar
  time_t t = time(NULL);
  struct tm *cal = localtime(&t);

  // set hour, minute and seconds
  cal->tm_hour = hour;
  cal->tm_min = minute;
  cal->tm_sec = seconds;

  // make time
  t = mktime(cal);

  // set time
  struct timeval tv = {.tv_sec = t};
  settimeofday(&tv, NULL);

  // update clock
  al_clock_update();
}

int64_t al_clock_get_epoch() {
  // get timestamp
  struct timeval tv;
  gettimeofday(&tv, NULL);

  return (int64_t)(tv.tv_sec) * 1000 + (int64_t)(tv.tv_usec) / 1000;
}

void al_clock_set_epoch(int64_t ts) {
  // set time
  struct timeval tv = {
      .tv_sec = ts / 1000,
      .tv_usec = (ts % 1000) * 1000,
  };
  settimeofday(&tv, NULL);

  // update clock
  al_clock_update();
}

void al_clock_epoch_time(int64_t ts, uint16_t *hour, uint16_t *minute, uint16_t *second) {
  // get time as calendar
  time_t t = ts / 1000;
  struct tm *cal = localtime(&t);

  // set hour and minute
  if (hour != NULL) *hour = cal->tm_hour;
  if (minute != NULL) *minute = cal->tm_min;
  if (second != NULL) *second = cal->tm_sec;
}

void al_clock_epoch_date(int64_t ts, uint16_t *year, uint16_t *month, uint16_t *day) {
  // get time as calendar
  time_t t = ts / 1000;
  struct tm *cal = localtime(&t);

  // set day, month and year
  if (year != NULL) *year = cal->tm_year + 1900;
  if (month != NULL) *month = cal->tm_mon + 1;
  if (day != NULL) *day = cal->tm_mday;
}
