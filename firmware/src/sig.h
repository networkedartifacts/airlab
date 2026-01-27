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
  SIG_STOP = (1 << 10),
  SIG_TOUCH = (1 << 11),
  SIG_SCROLL = (1 << 12),
  SIG_EJECT = (1 << 13),
  SIG_MOTION = (1 << 14),
  SIG_POWER = (1 << 15),
  SIG_LAUNCH = (1 << 16),
  SIG_KILL = (1 << 17),

  SIG_META = SIG_ENTER | SIG_ESCAPE,
  SIG_ARROWS = SIG_UP | SIG_DOWN | SIG_LEFT | SIG_RIGHT,
  SIG_KEYS = SIG_META | SIG_ARROWS,
  SIG_INTERRUPT = SIG_MOTION | SIG_POWER,
} sig_type_t;

typedef struct {
  sig_type_t type;
  union {
    bool repeat;     // keys
    float position;  // touch
    struct {
      float std;
      float fast;
    } scroll;
    struct {
      const char *file;
      const char *binary;
    } plugin;  // launch;
  };
} sig_event_t;

void sig_init();

void sig_dispatch(sig_event_t event);

sig_event_t sig_await(sig_type_t filter, int64_t timeout);

#endif  // SIG_H
