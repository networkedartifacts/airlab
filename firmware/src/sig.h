#ifndef SIG_H
#define SIG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  SIG_ANY = 0,
  SIG_TIMEOUT = (1 << 1),
  SIG_ENTER = (1 << 2),
  SIG_ESCAPE = (1 << 3),
  SIG_UP = (1 << 4),
  SIG_DOWN = (1 << 5),
  SIG_LEFT = (1 << 6),
  SIG_RIGHT = (1 << 7),
  SIG_SENSOR = (1 << 8),
  SIG_APPEND = (1 << 9),

  SIG_META = SIG_ENTER | SIG_ESCAPE,
  SIG_VERT = SIG_UP | SIG_DOWN,
  SIG_HOR = SIG_LEFT | SIG_RIGHT,
  SIG_ARROWS = SIG_VERT | SIG_HOR,
  SIG_KEYS = SIG_META | SIG_ARROWS,
} sig_event_t;

void sig_init();

void sig_dispatch(sig_event_t event);

sig_event_t sig_await(sig_event_t filter, uint32_t timeout);

#endif  // SIG_H
