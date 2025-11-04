#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define AL_W 296
#define AL_H 128

#define IMPORT(fn) __attribute__((import_module("env"), import_name(fn)))

/* primary operations */

typedef enum {
  AL_INFO_BATTERY_LEVEL,
  AL_INFO_BATTERY_VOLTAGE,
  AL_INFO_POWER_USB,
  AL_INGO_POWER_CHARGING,
  AL_INFO_SENSOR_TEMPERATURE,
  AL_INFO_SENSOR_HUMIDITY,
  AL_INFO_SENSOR_CO2,
  AL_INFO_SENSOR_VOC,
  AL_INFO_SENSOR_NOX,
  AL_INFO_SENSOR_PRESSURE,
  AL_INGO_STORE_SHORT,
  AL_INGO_STORE_LONG,
  AL_INFO_ACCEL_FRONT,
  AL_INFO_ACCEL_ROTATION,
  AL_INFO_STORAGE_INT,
  AL_INFO_STORAGE_EXT,
} al_info_t;

IMPORT("al_info") extern float al_info(al_info_t i);

typedef enum {
  ENG_CONFIG_BUTTON_REPEAT,
} al_config_t;

IMPORT("al_config") extern int al_config(al_config_t c, int a, int b, int d);

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

IMPORT("al_delay") extern void al_delay(int ms);

IMPORT("al_millis") extern int64_t al_millis();

/* interface operations */

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

IMPORT("al_draw") extern void al_draw(int x, int y, int w, int h, int s, int a, const void *i, const void *m);

typedef enum {
  AL_BEEP_WAIT = (1 << 0),
} al_beep_flags_t;

IMPORT("al_beep") extern void al_beep(float freq, int duration, al_beep_flags_t flags);

/* IO operations */

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

/* sprite operations */

IMPORT("al_sprite_resolve") extern int _al_sprite_resolve(const void *name, int name_len);
int al_sprite_resolve(const char *name) { return _al_sprite_resolve(name, strlen(name)); }

IMPORT("al_sprite_width") extern int al_sprite_width(int sprite);
IMPORT("al_sprite_height") extern int al_sprite_height(int sprite);

IMPORT("al_sprite_draw") extern void al_sprite_draw(int sprite, int x, int y, int s, int a);

/* data operations */

IMPORT("al_data_set") extern int _al_data_set(const void *name, int name_len, const void *buf, int buf_len);
int al_data_set(const char *name, const void *buf, int buf_len) {
  return _al_data_set((const void *)name, strlen(name), buf, buf_len);
}

IMPORT("al_data_get") extern int _al_data_get(const void *name, int name_len, void *buf, int buf_len);
int al_data_get(const char *name, void *buf, int buf_len) {
  return _al_data_get((const void *)name, strlen(name), buf, buf_len);
}

/* HTTP operations */

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

/* utils */

IMPORT("al_log") extern void _al_log(const void *msg, int msg_len);
extern void al_log(const char *msg) { _al_log(msg, strlen(msg)); }
extern void al_logf(const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (len > 0) {
    if (len >= sizeof(buf)) {
      len = sizeof(buf) - 1;
    }
    _al_log(buf, len);
  }
}
