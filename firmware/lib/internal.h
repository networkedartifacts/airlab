#ifndef AL_INTERNAL_H
#define AL_INTERNAL_H

#include "sensor_hal.h"

// data stored in RTC slow memory
#define AL_KEEP RTC_DATA_ATTR

#define AL_ACCEL_INT GPIO_NUM_16

#define AL_BUTTONS_A GPIO_NUM_12
#define AL_BUTTONS_B GPIO_NUM_17
#define AL_BUTTONS_C GPIO_NUM_15
#define AL_BUTTONS_D GPIO_NUM_4
#define AL_BUTTONS_E GPIO_NUM_9
#define AL_BUTTONS_F GPIO_NUM_13

esp_err_t al_i2c_transfer(uint8_t addr, uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len, int timeout);

void al_accel_init(bool reset);
void al_buttons_init();
void al_buzzer_init();
void al_clock_init();
void al_epd_init();
void al_led_init(bool reset);
void al_power_init();
void al_sensor_init(bool reset);
void al_touch_init(bool reset);

void al_touch_sleep();
void al_touch_wake();

void al_ulp_start();
void al_ulp_stop();
int al_ulp_readings();
al_sensor_raw_t al_ulp_get_reading(int index);

#endif  // AL_INTERNAL_H
