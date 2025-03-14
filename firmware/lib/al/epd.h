#ifndef AL_EPD_H
#define AL_EPD_H

#define AL_EPD_WIDTH 128
#define AL_EPD_HEIGHT 296
#define AL_EPD_FRAME (AL_EPD_WIDTH / 8 * AL_EPD_HEIGHT)

void al_epd_set(uint8_t *data, uint16_t x, uint16_t y, bool black);

void al_epd_update(uint8_t *frame, bool partial);

void al_epd_sleep();

#endif  // AL_EPD_H
