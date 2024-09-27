#ifndef EPD_H
#define EPD_H

#define EPD_WIDTH 128
#define EPD_HEIGHT 296
#define EPD_FRAME (EPD_WIDTH / 8 * EPD_HEIGHT)

void epd_init();

void epd_set(uint8_t *data, uint16_t x, uint16_t y, bool black);

void epd_update(uint8_t *frame, bool partial);

void epd_sleep();

#endif  // EPD_H
