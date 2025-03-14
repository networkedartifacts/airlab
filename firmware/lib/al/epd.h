#ifndef AL_EPD_H
#define AL_EPD_H

#include <stdbool.h>
#include <stdint.h>

/**
 * The display size and frame size.
 */
#define AL_EPD_WIDTH 128
#define AL_EPD_HEIGHT 296
#define AL_EPD_FRAME (AL_EPD_WIDTH / 8 * AL_EPD_HEIGHT)

/**
 * Helper function to set a pixel in a frame.
 *
 * @param data The frame data.
 * @param x The x coordinate.
 * @param y The y coordinate.
 * @param black Whether to set the pixel black or white.
 */
void al_epd_set(uint8_t *data, uint16_t x, uint16_t y, bool black);

/**
 * Write a frame to the display.
 *
 * @param frame The frame to write.
 * @param partial Whether to do a partial update.
 */
void al_epd_update(uint8_t *frame, bool partial);

#endif  // AL_EPD_H
