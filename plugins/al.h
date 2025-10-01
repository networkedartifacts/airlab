#include <string.h>

#define IMPORT(fn) __attribute__((import_module("env"), import_name(fn)))

IMPORT("al_clear") extern void al_clear(int c);

typedef enum {
  AL_WRITE_ALIGN_CENTER = (1 << 0),
  AL_WRITE_ALIGN_RIGHT = (1 << 1),
} al_write_flags_t;

IMPORT("al_write")
extern void _al_write(int x, int y, int s, int f, int c, const void *sp, int sl, int flags);

void al_write(int x, int y, int s, int f, int c, const char *str, al_write_flags_t flags) {
  _al_write(x, y, s, f, c, str, strlen(str), flags);
}

IMPORT("al_rect") extern void al_rect(int x, int y, int w, int h, int c, int b);

typedef enum {
  AL_YIELD_SKIP_FRAME = (1 << 0),
  AL_YIELD_WAIT_FRAME = (1 << 1),
  AL_YIELD_INVERT = (1 << 2),
  AL_YIELD_REFRESH = (1 << 3),
} al_yield_flags_t;

typedef enum {
  AL_YIELD_TIMEOUT = 0,
  AL_YIELD_ENTER = 1,
  AL_YIELD_ESCAPE = 2,
  AL_YIELD_UP = 3,
  AL_YIELD_DOWN = 4,
  AL_YIELD_LEFT = 5,
  AL_YIELD_RIGHT = 6,
} al_yield_result_t;

IMPORT("al_yield") extern int _al_yield(int timeout, int flags);

al_yield_result_t al_yield(int timeout, al_yield_flags_t flags) { return _al_yield(timeout, flags); }

/* I/O */

typedef enum {
  AL_GPIO_CONFIG,
  AL_GPIO_WRITE,
  AL_GPIO_READ,
} al_gpio_cmd_t;

typedef enum {
  AL_GPIO_A = (1 << 0),
  AL_GPIO_B = (1 << 1),
  AL_GPIO_HIGH = (1 << 2),   // or low
  AL_GPIO_INPUT = (1 << 3),  // or output
  AL_GPIO_PULL_UP = (1 << 4),
  AL_GPIO_PULL_DOWN = (1 << 5),
} al_gpio_flags_t;

IMPORT("al_gpio") extern int _al_gpio(int cmd, int flags);

int al_gpio(al_gpio_cmd_t cmd, al_gpio_flags_t flags) { return _al_gpio(cmd, flags); }

IMPORT("al_i2c")
extern int al_i2c(int addr, const void *w, int wl, void *r, int rl, int timeout);
