#ifndef ACC_H
#define ACC_H

typedef enum {
  ACC_FRONT = 0b0,
  ACC_BACK = 0b1,
} acc_face_t;

typedef enum {
  ACC_PORTRAIT_UP = 0b00,
  ACC_PORTRAIT_DOWN = 0b01,
  ACC_LANDSCAPE_RIGHT = 0b10,
  ACC_LANDSCAPE_LEFT = 0b11,
} acc_mode_t;

void acc_init();

#endif  // ACC_H
