#ifndef RTC_H
#define RTC_H

typedef struct {
  uint8_t hours;   /* 0-23 */
  uint8_t minutes; /* 0-59 */
  uint8_t seconds; /* 0-59 */
  uint8_t weekday; /* 1-7 */
  uint8_t day;     /* 1-31 */
  uint8_t month;   /* 1-12 */
  uint8_t year;    /* 0-99 */
} rtc_state_t;

void rtc_sync();
rtc_state_t rtc_get();
void rtc_set(rtc_state_t state);

#endif  // RTC_H
