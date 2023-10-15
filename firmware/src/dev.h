#ifndef DEV_H
#define DEV_H

#include <esp_attr.h>

#define DEV_KEEP RTC_DATA_ATTR

#define DEV_VERSION "0.1.2"

// Board Type Selection:
// 0: Single Board Prototype
// 1: AL2304-1 Boards
#define DEV_BOARD 1

// Development Mode
// 0: Off
// 1: On
#define DEV_MODE 0

void dev_init();

uint8_t dev_shift();

#endif  // DEV_H
