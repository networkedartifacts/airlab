#ifndef AL_CLOCK_H
#define AL_CLOCK_H

/**
 * Get/Set the current date.
 *
 * @param year The year.
 * @param month The month.
 * @param day The day.
 */
void al_clock_get_date(uint16_t *year, uint16_t *month, uint16_t *day);
void al_clock_set_date(uint16_t year, uint16_t month, uint16_t day);

/**
 * Get/Set the current time.
 *
 * @param hour The hour.
 * @param minute The minute.
 * @param seconds The seconds.
 */
void al_clock_get_time(uint16_t *hour, uint16_t *minute, uint16_t *seconds);
void al_clock_set_time(uint16_t hour, uint16_t minute, uint16_t seconds);

/**
 * Get the current epoch time.
 *
 * @return The current epoch time.
 */
int64_t al_clock_get_epoch();

/**
 * Convert an epoch time to a date.
 *
 * @param ts The epoch time.
 * @param hour The hour.
 * @param minute The minute.
 * @param second The second.
 */
void al_clock_conv_epoch(int64_t ts, uint16_t *hour, uint16_t *minute, uint16_t *second);

#endif  // AL_CLOCK_H
