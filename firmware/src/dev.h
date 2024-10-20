#ifndef DEV_H
#define DEV_H

#include <esp_attr.h>

#define DEV_KEEP RTC_DATA_ATTR

#define DEV_VERSION "0.2.0"

#define DEV_DEBUG_MENU false
#define DEV_RECORD_SCREEN false

#define DEV_BTN_A GPIO_NUM_12
#define DEV_BTN_B GPIO_NUM_17
#define DEV_BTN_C GPIO_NUM_15
#define DEV_BTN_D GPIO_NUM_4
#define DEV_BTN_E GPIO_NUM_9
#define DEV_BTN_F GPIO_NUM_13

#define DEV_INT_ACC GPIO_NUM_16

void dev_init();

#endif  // DEV_H
