#ifndef AL_INTERNAL_H
#define AL_INTERNAL_H

#define AL_ACCEL_INT GPIO_NUM_16

#define AL_BUTTONS_A GPIO_NUM_12
#define AL_BUTTONS_B GPIO_NUM_17
#define AL_BUTTONS_C GPIO_NUM_15
#define AL_BUTTONS_D GPIO_NUM_4
#define AL_BUTTONS_E GPIO_NUM_9
#define AL_BUTTONS_F GPIO_NUM_13

void al_accel_init();
void al_buttons_init();
void al_buzzer_init();
void al_clock_init();
void al_epd_init();
void al_led_init();
void al_power_init();
void al_sensor_init();
void al_touch_init();

void al_touch_sleep();
void al_touch_wake();

#endif // AL_INTERNAL_H
