#ifndef ACC_H
#define ACC_H

typedef struct {
  bool front;
  uint16_t rot;
  bool lock;
} acc_state_t;

void acc_init();
acc_state_t acc_get();

#endif  // ACC_H
