#ifndef DEV_H
#define DEV_H

#include <esp_attr.h>

#define DEV_KEEP RTC_DATA_ATTR

#define DEV_VERSION "0.1.2"

// Development Mode
// 0: Off
// 1: On
#define DEV_MODE 0

void dev_init();

#endif  // DEV_H
