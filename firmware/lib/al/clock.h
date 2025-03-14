#ifndef AL_CLOCK_H
#define AL_CLOCK_H

typedef struct {
  uint8_t hours;   /* 0-23 */
  uint8_t minutes; /* 0-59 */
  uint8_t seconds; /* 0-59 */
  uint8_t weekday; /* 1-7 */
  uint8_t day;     /* 1-31 */
  uint8_t month;   /* 1-12 */
  uint16_t year;   /* 2000-2099 */
} al_clock_state_t;

al_clock_state_t al_clock_get();
void al_clock_set(al_clock_state_t state);

#endif  // AL_CLOCK_H
