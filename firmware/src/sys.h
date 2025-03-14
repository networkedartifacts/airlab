#ifndef SYS_H
#define SYS_H

#include <stdint.h>

void sys_get_date(uint16_t *year, uint16_t *month, uint16_t *day);
void sys_set_date(uint16_t year, uint16_t month, uint16_t day);

void sys_get_time(uint16_t *hour, uint16_t *minute, uint16_t *seconds);
void sys_set_time(uint16_t hour, uint16_t minute, uint16_t seconds);

int64_t sys_get_timestamp();
void sys_conv_timestamp(int64_t ts, uint16_t *hour, uint16_t *minute, uint16_t *seconds);

#endif  // SYS_H
