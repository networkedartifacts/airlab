#ifndef AL_LED_H
#define AL_LED_H

/**
 * Set the LED color.
 *
 * @param r Red component.
 * @param g Green component.
 * @param b Blue component.
 */
void al_led_set(float r, float g, float b);

/**
 * Periodically flash the LED with a color.
 *
 * @param r Red component.
 * @param g Green component.
 * @param b Blue component.
 */
void al_led_flash(float r, float g, float b);

#endif  // AL_LED_H
