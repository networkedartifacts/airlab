#include <string.h>

#define AL_W 296
#define AL_H 128

#define IMPORT(fn) __attribute__((import_module("env"), import_name(fn)))

IMPORT("al_clear") extern void al_clear(int c);

IMPORT("al_line") extern void al_line(int x1, int y1, int x2, int y2, int c, int b);

IMPORT("al_rect") extern void al_rect(int x, int y, int w, int h, int c, int b);

typedef enum {
  AL_WRITE_ALIGN_CENTER = (1 << 0),
  AL_WRITE_ALIGN_RIGHT = (1 << 1),
} al_write_flags_t;

IMPORT("al_write")
extern void _al_write(int x, int y, int s, int f, int c, const void *sp, int sl, int flags);

void al_write(int x, int y, int s, int f, int c, const char *str, al_write_flags_t flags) {
  _al_write(x, y, s, f, c, str, strlen(str), flags);
}

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

/* Sprites */

IMPORT("al_sprite_resolve") extern int _al_sprite_resolve(const void *name, int name_len);

int al_sprite_resolve(const char *name) { return _al_sprite_resolve(name, strlen(name)); }

IMPORT("al_sprite_width") extern int al_sprite_width(int sprite);
IMPORT("al_sprite_height") extern int al_sprite_height(int sprite);

IMPORT("al_sprite_draw") extern void al_sprite_draw(int sprite, int x, int y, int s);

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

/* HTTP */

enum {
  // request
  ENG_HTTP_URL,       // string
  ENG_HTTP_METHOD,    // string
  ENG_HTTP_USERNAME,  // string
  ENG_HTTP_PASSWORD,  // string
  ENG_HTTP_HEADER,    // string, string
  ENG_HTTP_TIMEOUT,   // int (ms)

  // response
  ENG_HTTP_STATUS,  // int
  ENG_HTTP_LENGTH,  // int
  ENG_HTTP_ERRNO,   // int
};

IMPORT("al_http_new") extern void al_http_new();

IMPORT("al_http_set")
extern int _al_http_set(int field, int num, void *str1, int str1_len, void *str2, int str2_len);
int al_http_set(int field, int num, const char *str1, const char *str2) {
  return _al_http_set(field, num, (void *)str1, str1 ? strlen(str1) : 0, (void *)str2, str2 ? strlen(str2) : 0);
}

IMPORT("al_http_run")
extern int al_http_run(void *req, int req_len, void *res, int res_len);

IMPORT("al_http_get") extern int al_http_get(int field);
