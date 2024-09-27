#ifndef DEV_H
#define DEV_H

#include <esp_attr.h>

#define DEV_KEEP RTC_DATA_ATTR

#define DEV_VERSION "0.2.0"

#define DEV_DEBUG_MENU false
#define DEV_RECORD_SCREEN false

void dev_init();

#endif  // DEV_H
