#ifndef AL_TOUCH_H
#define AL_TOUCH_H

#include <stdint.h>

typedef struct {
  uint8_t touches;
  float position;
  float delta;
} al_touch_event_t;

typedef void (*al_touch_hook_t)(al_touch_event_t);

void al_touch_config(al_touch_hook_t hook);

#endif  // AL_TOUCH_H
