#include <sys/time.h>

#include "sys.h"
#include "dev.h"

DEV_KEEP bool sys_date = false;
DEV_KEEP bool sys_time = false;

bool sys_has_date() { return sys_date; }

void sys_get_date(uint16_t *year, uint16_t *month, uint16_t *day) {
  // get time as calendar
  time_t t = time(NULL);
  struct tm *cal = gmtime(&t);

  // set day, month and year
  *year = cal->tm_year + 1900;
  *month = cal->tm_mon + 1;
  *day = cal->tm_mday;
}

void sys_set_date(uint16_t year, uint16_t month, uint16_t day) {
  // get time as calendar
  time_t t = time(NULL);
  struct tm *cal = gmtime(&t);

  // set day, month and year
  cal->tm_year = year - 1900;
  cal->tm_mon = month - 1;
  cal->tm_mday = day;

  // make time
  t = mktime(cal);

  // set time
  struct timeval tv = {.tv_sec = t};
  settimeofday(&tv, NULL);

  // set flag
  sys_date = true;
}

bool sys_has_time() { return sys_time; }

void sys_get_time(uint16_t *hour, uint16_t *minute) {
  // get time as calendar
  time_t t = time(NULL);
  struct tm *cal = gmtime(&t);

  // set hour and minute
  *hour = cal->tm_hour;
  *minute = cal->tm_min;
}

void sys_set_time(uint16_t hour, uint16_t minute) {
  // get time as calendar
  time_t t = time(NULL);
  struct tm *cal = gmtime(&t);

  // set hour and minute
  cal->tm_hour = hour;
  cal->tm_min = minute;

  // make time
  t = mktime(cal);

  // set time
  struct timeval tv = {.tv_sec = t};
  settimeofday(&tv, NULL);

  // set flag
  sys_time = true;
}

int64_t sys_get_timestamp() {
  // get timestamp
  struct timeval tv;
  gettimeofday(&tv, NULL);

  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
