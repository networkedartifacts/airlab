#ifndef AL_INTERNAL_H
#define AL_INTERNAL_H

#include <stdbool.h>
#include <esp_err.h>
#include <esp_attr.h>

#include "sensor_hal.h"

// data stored in RTC slow memory
#define AL_KEEP RTC_DATA_ATTR

#define AL_INT_IN GPIO_NUM_16

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
void al_storage_init();
void al_store_init();
void al_touch_init(bool reset);
void al_ulp_init(bool reset);

void al_accel_check();

void al_clock_update();

void al_power_check();

void al_sensor_low_power(bool on, bool manual);

void al_touch_sleep();
void al_touch_wake();

void al_ulp_stop();
void al_ulp_start();
void al_ulp_load_state(al_sensor_hal_state_t* state);
int al_ulp_readings();
al_sensor_hal_data_t al_ulp_get_reading(int index);

#endif  // AL_INTERNAL_H
