#include "../al.h"

int main() {
  for (;;) {
    // read sensor
    uint8_t reg = 0x01;
    uint8_t buf[6] = {0};
    al_i2c(0x18, &reg, 1, buf, 6, 500);

    // get values
    int16_t x = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t y = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t z = (int16_t)((buf[5] << 8) | buf[4]);

    // apply
    float xx = x * 0.000976f;
    float yy = y * 0.000976f;
    float zz = z * 0.000976f;

    // format data
    char buffer[AL_H];
    snprintf(buffer, sizeof(buffer), "X: %.2f\nY: %.2f\nZ: %.2f", xx, yy, zz);

    // display data
    al_clear(0);
    al_write(AL_W / 2, AL_H / 2 - 30, 4, 16, 1, buffer, AL_WRITE_ALIGN_CENTER);

    // wait a bit
    al_yield_result_t res = al_yield(1000, 0);

    // stop on escape
    if (res == AL_YIELD_ESCAPE) {
      break;
    }
  }

  return 0;
}
