#include <naos.h>
#include <naos/sys.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wasm_export.h>
#include <bh_platform.h>
#include <lvgl.h>
#include <driver/gpio.h>
#include <esp_http_client.h>
#include <esp_log.h>

#include <al/core.h>

#include "fnt.h"
#include "gfx.h"
#include "gui.h"
#include "lvx.h"
#include "sig.h"
#include "eng_bundle.h"

#define ENG_DEBUG true

typedef struct {
  eng_bundle_t *bundle;
  lv_obj_t *canvas;
} eng_context_t;

/* memory helpers */

static void *eng_malloc(unsigned size) {
  // perform alloc 8-byte aligned
  return heap_caps_aligned_alloc(8, size, MALLOC_CAP_SPIRAM);
}

static void *eng_realloc(void *ptr, unsigned size) {
  // TODO: Also 8-byte align?
  // perform realloc
  return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
}

static void eng_free(void *ptr) {
  // perform free
  free(ptr);
}

/* utility helpers */

static lv_color_t eng_color(int c) {
  // determine color
  return c == 1 ? lv_color_black() : lv_color_white();
}

static const lv_font_t *eng_font(int f) {
  // determine font
  switch (f) {
    case 16:
      return &fnt_16;
    case 24:
      return &fnt_24;
    default:
      return &fnt_8;
  }
}

static bool eng_get_bit(const uint8_t *buf, size_t pos) {
  // get bit
  size_t byte = pos / 8;
  size_t bit = pos % 8;

  return buf[byte] & (1 << bit) ? 1 : 0;
}

static char *eng_mkstr(const uint8 *buf, int len) {
  // check length
  if (len <= 0) {
    return NULL;
  }

  // copy string
  char *str = eng_malloc(len + 1);
  memcpy(str, buf, len);
  str[len] = 0;

  return str;
}

/* primary operations */

static void eng_op_clear(wasm_exec_env_t env, int c) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_clear: c=%d", c);
  }

  // get context
  eng_context_t *ctx = wasm_runtime_get_user_data(env);

  // clear canvas
  lv_canvas_fill_bg(ctx->canvas, eng_color(c), LV_OPA_COVER);
}

static void eng_op_line(wasm_exec_env_t env, int x1, int y1, int x2, int y2, int c, int b) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_line: x1=%d, y1=%d, x2=%d, y2=%d, c=%d, b=%d", x1, y1, x2, y2, c, b);
  }

  // get context
  eng_context_t *ctx = wasm_runtime_get_user_data(env);

  // prepare descriptor
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = eng_color(c);
  line_dsc.width = b;

  // draw line
  lv_point_t points[2] = {{x1, y1}, {x2, y2}};
  lv_canvas_draw_line(ctx->canvas, points, 2, &line_dsc);
}

static void eng_op_rect(wasm_exec_env_t env, int x, int y, int w, int h, int c, int b) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_rect: x=%d, y=%d, w=%d, h=%d, c=%d, b=%d", x, y, w, h, c, b);
  }

  // get context
  eng_context_t *ctx = wasm_runtime_get_user_data(env);

  // draw rectangle
  lv_draw_rect_dsc_t rect_dsc;
  lv_draw_rect_dsc_init(&rect_dsc);
  rect_dsc.bg_color = eng_color(c);
  rect_dsc.bg_opa = b > 0 ? LV_OPA_TRANSP : LV_OPA_COVER;
  rect_dsc.border_color = eng_color(c);
  rect_dsc.border_width = b;
  lv_canvas_draw_rect(ctx->canvas, x, y, w, h, &rect_dsc);
}

typedef enum {
  ENG_WRITE_ALIGN_CENTER = (1 << 0),
  ENG_WRITE_ALIGN_RIGHT = (1 << 1),
} eng_write_flags_t;

static void eng_op_write(wasm_exec_env_t env, int x, int y, int s, int f, int c, uint8 *text, int text_len, int flags) {
  // copy text
  char copy[128];
  if (text_len >= sizeof(copy)) {
    text_len = sizeof(copy) - 1;
  }
  memcpy(copy, text, text_len);
  copy[text_len] = 0;

  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_write: x=%d, y=%d, s=%d, f=%d, c=%d, s='%s' flags=%d", x, y, s, f, c, copy, flags);
  }

  // get context
  eng_context_t *ctx = wasm_runtime_get_user_data(env);

  // calculate text width
  int w = lv_txt_get_width(copy, text_len, eng_font(f), 0, LV_TEXT_FLAG_NONE);

  // apply alignment
  if (flags & ENG_WRITE_ALIGN_CENTER) {
    x -= w / 2;
  } else if (flags & ENG_WRITE_ALIGN_RIGHT) {
    x -= w;
  }

  // prepare descriptor
  lv_draw_label_dsc_t label_dsc;
  lv_draw_label_dsc_init(&label_dsc);
  label_dsc.color = eng_color(c);
  label_dsc.font = eng_font(f);
  label_dsc.line_space = s;
  label_dsc.align = LV_TEXT_ALIGN_LEFT;
  if (flags & ENG_WRITE_ALIGN_CENTER) {
    label_dsc.align = LV_TEXT_ALIGN_CENTER;
  } else if (flags & ENG_WRITE_ALIGN_RIGHT) {
    label_dsc.align = LV_TEXT_ALIGN_RIGHT;
  }
  label_dsc.flag = LV_TEXT_FLAG_FIT;

  // draw text
  lv_canvas_draw_text(ctx->canvas, x, y, w, &label_dsc, copy);
}

static void eng_op_draw(wasm_exec_env_t env, int x, int y, int w, int h, int s, const uint8 *i, const uint8 *m) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_draw: x=%d, y=%d, w=%d, h=%d, s=%d", x, y, w, h, s);
  }

  // check dimensions
  if (w <= 0 || h <= 0 || s <= 0) {
    return;
  }

  // get context
  eng_context_t *ctx = wasm_runtime_get_user_data(env);

  // prepare sprite
  lvx_sprite_t sprite = {
      .w = w,
      .h = h,
      .s = s,
      .img = i,
      .mask = m,
  };

  // prepare image
  lv_img_dsc_t img = lvx_sprite_img(&sprite);

  // prepare descriptor
  lv_draw_img_dsc_t img_draw;
  lv_draw_img_dsc_init(&img_draw);

  // draw image
  lv_canvas_draw_img(ctx->canvas, x, y, &img, &img_draw);
}

typedef enum {
  ENG_YF_SKIP_FRAME = (1 << 0),
  ENG_YF_WAIT_FRAME = (1 << 1),
  ENG_YF_INVERT = (1 << 2),
  ENG_YF_REFRESH = (1 << 3),
} eng_yield_flags_t;

typedef enum {
  ENG_YIELD_TIMEOUT = 0,
  ENG_YIELD_ENTER = 1,
  ENG_YIELD_ESCAPE = 2,
  ENG_YIELD_UP = 3,
  ENG_YIELD_DOWN = 4,
  ENG_YIELD_LEFT = 5,
  ENG_YIELD_RIGHT = 6,
} eng_yield_result_t;

static int eng_op_yield(wasm_exec_env_t _, int timeout, int flags) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_yield: timeout=%d flags=%d", timeout, flags);
  }

  // unlock graphics
  gfx_end(flags & ENG_YF_SKIP_FRAME, flags & ENG_YF_WAIT_FRAME);

  // await event or deadline
  sig_event_t event = sig_await(SIG_KEYS, timeout);

  // handle events
  int ret = 0;
  switch (event.type) {
    case SIG_TIMEOUT:
      ret = ENG_YIELD_TIMEOUT;
      break;
    case SIG_ENTER:
      ret = ENG_YIELD_ENTER;
      break;
    case SIG_ESCAPE:
      ret = ENG_YIELD_ESCAPE;
      break;
    case SIG_UP:
      ret = ENG_YIELD_UP;
      break;
    case SIG_DOWN:
      ret = ENG_YIELD_DOWN;
      break;
    case SIG_LEFT:
      ret = ENG_YIELD_LEFT;
      break;
    case SIG_RIGHT:
      ret = ENG_YIELD_RIGHT;
      break;
    default:
      break;
  }

  // lock graphics
  gfx_begin(flags & ENG_YF_REFRESH, flags & ENG_YF_INVERT);

  return ret;
}

static int64_t eng_op_millis(wasm_exec_env_t _) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_millis");
  }

  // return time
  return naos_millis();
}

/* sprite operations */

static int eng_op_sprite_resolve(wasm_exec_env_t env, uint8 *name, int name_len) {
  // copy name
  char copy[64];
  if (name_len >= sizeof(copy)) {
    name_len = sizeof(copy) - 1;
  }
  memcpy(copy, name, name_len);
  copy[name_len] = 0;

  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_sprite_resolve: name='%s'", copy);
  }

  // get context
  eng_context_t *ctx = wasm_runtime_get_user_data(env);

  // locate sprite
  return eng_bundle_locate(ctx->bundle, ENG_BUNDLE_TYPE_SPRITE, copy, NULL);
}

static int eng_op_sprite_width(wasm_exec_env_t env, int sprite) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_sprite_width: sprite=%d", sprite);
  }

  // get context
  eng_context_t *ctx = wasm_runtime_get_user_data(env);

  // check sprite
  eng_bundle_section_t *section = &ctx->bundle->sections[sprite];
  if (sprite < 0 || sprite >= ctx->bundle->sections_num || section->type != ENG_BUNDLE_TYPE_SPRITE) {
    return -1;
  }

  // parse sprite
  eng_bundle_sprite_t sp;
  if (!eng_bundle_parse_sprite(&sp, ctx->bundle, section)) {
    naos_log("eng_op_sprite_draw: parsing sprite %d failed", sprite);
    return -1;
  }

  return sp.width;
}

static int eng_op_sprite_height(wasm_exec_env_t env, int sprite) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_sprite_height: sprite=%d", sprite);
  }

  // get context
  eng_context_t *ctx = wasm_runtime_get_user_data(env);

  // check sprite
  eng_bundle_section_t *section = &ctx->bundle->sections[sprite];
  if (sprite < 0 || sprite >= ctx->bundle->sections_num || section->type != ENG_BUNDLE_TYPE_SPRITE) {
    return -1;
  }

  // parse sprite
  eng_bundle_sprite_t sp;
  if (!eng_bundle_parse_sprite(&sp, ctx->bundle, section)) {
    naos_log("eng_op_sprite_draw: parsing sprite %d failed", sprite);
    return -1;
  }

  return sp.height;
}

static void eng_op_sprite_draw(wasm_exec_env_t env, int sprite, int x, int y, int s) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_sprite_draw: sprite=%d, x=%d, y=%d, s=%d", sprite, x, y, s);
  }

  // get context
  eng_context_t *ctx = wasm_runtime_get_user_data(env);

  // check sprite
  eng_bundle_section_t *section = &ctx->bundle->sections[sprite];
  if (sprite < 0 || sprite >= ctx->bundle->sections_num || section->type != ENG_BUNDLE_TYPE_SPRITE) {
    return;
  }

  // parse sprite
  eng_bundle_sprite_t sp;
  if (!eng_bundle_parse_sprite(&sp, ctx->bundle, section)) {
    naos_log("eng_op_sprite_draw: parsing sprite %d failed", sprite);
    return;
  }

  // draw sprite
  eng_op_draw(env, x, y, sp.width, sp.height, s, sp.image, sp.mask);
}

static int eng_op_sprite_read(wasm_exec_env_t env, int sprite, int x, int y) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_sprite_read: sprite=%d, x=%d, y=%d", sprite, x, y);
  }

  // get context
  eng_context_t *ctx = wasm_runtime_get_user_data(env);

  // check sprite
  eng_bundle_section_t *section = &ctx->bundle->sections[sprite];
  if (sprite < 0 || sprite >= ctx->bundle->sections_num || section->type != ENG_BUNDLE_TYPE_SPRITE) {
    return -1;
  }

  // parse sprite
  eng_bundle_sprite_t sp;
  if (!eng_bundle_parse_sprite(&sp, ctx->bundle, section)) {
    naos_log("eng_op_sprite_draw: parsing sprite %d failed", sprite);
    return -1;
  }

  // check parameters
  if (x < 0 || y < 0 || x >= sp.width || y >= sp.height) {
    return -1;
  }

  // compute index
  int idx = y * sp.width + x;

  // test mask
  if (eng_get_bit(sp.mask, idx) == 0) {
    return -1;
  }

  // test image
  if (eng_get_bit(sp.image, idx) != 0) {
    return 1;
  } else {
    return 0;
  }
}

/* IO operations */

typedef enum {
  ENG_GPIO_CONFIG,
  ENG_GPIO_WRITE,
  ENG_GPIO_READ,
} eng_gpio_cmd_t;

typedef enum {
  ENG_GPIO_A = (1 << 0),
  ENG_GPIO_B = (1 << 1),
  ENG_GPIO_HIGH = (1 << 2),   // or low
  ENG_GPIO_INPUT = (1 << 3),  // or output
  ENG_GPIO_PULL_UP = (1 << 4),
  ENG_GPIO_PULL_DOWN = (1 << 5),
} eng_gpio_flags_t;

static int eng_op_gpio(wasm_exec_env_t _, int cmd, int flags) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_gpio: cmd=%d, flags=0x%X", cmd, flags);
  }

  // determine GPIO num
  gpio_num_t num = 0;
  if (flags & ENG_GPIO_A) {
    num = AL_GPIO_A;
  } else if (flags & ENG_GPIO_B) {
    num = AL_GPIO_B;
  } else {
    return -1;
  }

  // handle commands
  switch (cmd) {
    case ENG_GPIO_CONFIG: {
      // configure GPIO
      gpio_config_t io_conf = {
          .pin_bit_mask = BIT64(num),
          .mode = flags & ENG_GPIO_INPUT ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT,
          .pull_up_en = flags & ENG_GPIO_PULL_UP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
          .pull_down_en = flags & ENG_GPIO_PULL_DOWN ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
          .intr_type = GPIO_INTR_DISABLE,
      };
      esp_err_t err = gpio_config(&io_conf);

      return err == ESP_OK ? 0 : -1;
    }
    case ENG_GPIO_WRITE: {
      // set GPIO level
      int level = (flags & ENG_GPIO_HIGH) ? 1 : 0;
      esp_err_t err = gpio_set_level(num, level);

      return err == ESP_OK ? 0 : -1;
    }
    case ENG_GPIO_READ: {
      // get GPIO level
      int level = gpio_get_level(num);

      return level;
    }
    default:
      return -1;
  }
}

static int eng_op_i2c(wasm_exec_env_t _, int addr, uint8 *tx, int tx_len, uint8 *rx, int rx_len, int timeout) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_i2c: addr=%d tx=%d rx=%d timeout=%d", addr, tx_len, rx_len, timeout);
  }

  // perform transfer
  esp_err_t err = al_i2c_transfer(addr, tx, tx_len, rx, rx_len, timeout);

  return err == ESP_OK ? 0 : -1;
}

/* HTTP operations */

static esp_http_client_config_t eng_http_cfg = {0};
static esp_http_client_handle_t eng_http_client = {0};

enum {
  // request
  ENG_HTTP_URL,       // string
  ENG_HTTP_METHOD,    // string
  ENG_HTTP_USERNAME,  // string
  ENG_HTTP_PASSWORD,  // string
  ENG_HTTP_HEADER,    // string, string
  ENG_HTTP_TIMEOUT,   // int (ms)
  // CERT, TLS

  // response
  ENG_HTTP_STATUS,  // int
  ENG_HTTP_LENGTH,  // int
  ENG_HTTP_ERRNO,   // int
};

static esp_err_t eng_http_handler(esp_http_client_event_t *evt) {
  // get value
  naos_value_t *val = evt->user_data;

  // track length
  static size_t len;

  // handle vents
  switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
    case HTTP_EVENT_ON_CONNECTED:
    case HTTP_EVENT_HEADER_SENT:
    case HTTP_EVENT_ON_HEADER:
      break;
    case HTTP_EVENT_ON_DATA:
      // clean the buffer on first call
      if (len == 0) {
        memset(val->buf, 0, val->len);
      }

      // determine chunk
      size_t chunk = evt->data_len;
      if (chunk > (val->len - len)) {
        chunk = val->len - len;
      }

      // copy chunk
      if (chunk) {
        memcpy(val->buf + len, evt->data, chunk);
      }

      // increment
      len += chunk;

      break;
    case HTTP_EVENT_ON_FINISH:
    case HTTP_EVENT_DISCONNECTED:
      len = 0;
      break;
    case HTTP_EVENT_REDIRECT:
      break;
  }

  return ESP_OK;
}

static void eng_op_http_new() {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_http_new");
  }

  // destroy previous client
  if (eng_http_client) {
    ESP_ERROR_CHECK(esp_http_client_cleanup(eng_http_client));
  }

  // initialize config
  memset(&eng_http_cfg, 0, sizeof(esp_http_client_config_t));
  eng_http_cfg.url = "http://networkedartifacts.com";
  eng_http_cfg.max_redirection_count = 3;
  eng_http_cfg.max_authorization_retries = -1;
  eng_http_cfg.buffer_size = 1024;
  eng_http_cfg.buffer_size_tx = 1024;
  eng_http_cfg.event_handler = eng_http_handler;
  eng_http_cfg.transport_type = HTTP_TRANSPORT_OVER_TCP;

  // create client
  eng_http_client = esp_http_client_init(&eng_http_cfg);
  if (!eng_http_client) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }
}

static int eng_op_http_set(wasm_exec_env_t _, int field, int num, uint8 *str, int str_len, uint8 *str2, int str2_len) {
  // copy strings
  char *str_copy = eng_mkstr(str, str_len);
  char *str2_copy = eng_mkstr(str2, str2_len);

  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_http_set: field=%d, num=%d, str='%s'", field, num, str_copy ? str_copy : "");
  }

  // handle fields
  switch (field) {
    case ENG_HTTP_URL:
      esp_http_client_set_url(eng_http_client, str_copy);  // makes copy
      break;
    case ENG_HTTP_METHOD:
      if (strcmp(str_copy, "GET") == 0) {
        esp_http_client_set_method(eng_http_client, HTTP_METHOD_GET);
      } else if (strcmp(str_copy, "POST") == 0) {
        esp_http_client_set_method(eng_http_client, HTTP_METHOD_POST);
      } else if (strcmp(str_copy, "PUT") == 0) {
        esp_http_client_set_method(eng_http_client, HTTP_METHOD_PUT);
      } else if (strcmp(str_copy, "PATH") == 0) {
        esp_http_client_set_method(eng_http_client, HTTP_METHOD_PATCH);
      } else if (strcmp(str_copy, "DELETE") == 0) {
        esp_http_client_set_method(eng_http_client, HTTP_METHOD_DELETE);
      } else {
        return -1;
      }
      break;
    case ENG_HTTP_USERNAME:
      esp_http_client_set_username(eng_http_client, str_copy);  // makes copy
      break;
    case ENG_HTTP_PASSWORD:
      esp_http_client_set_password(eng_http_client, str_copy);  // makes copy
      break;
    case ENG_HTTP_HEADER:
      esp_http_client_set_header(eng_http_client, str_copy, str2_copy);  // makes copy
      break;
    case ENG_HTTP_TIMEOUT:
      esp_http_client_set_timeout_ms(eng_http_client, num);
      break;
    default:
      return -1;
  }

  // free
  eng_free(str_copy);
  eng_free(str2_copy);

  return 0;
}

static int eng_op_http_run(wasm_exec_env_t _, uint8 *req, int req_len, uint8 *res, int res_len) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_http_run: req_len=%d, res_len=%d", req_len, res_len);
  }

  // set request data
  if (req && req_len > 0) {
    esp_http_client_set_post_field(eng_http_client, (const char *)req, req_len);
  } else {
    esp_http_client_set_post_field(eng_http_client, NULL, 0);
  }

  // set response buffer
  if (res && res_len > 0) {
    naos_value_t val = {.buf = res, .len = res_len};
    esp_http_client_set_user_data(eng_http_client, &val);
  } else {
    esp_http_client_set_user_data(eng_http_client, NULL);
  }

  // perform request
  esp_err_t err = esp_http_client_perform(eng_http_client);

  return err;
}

static int eng_op_http_get(wasm_exec_env_t _, int field) {
  // log
  if (ENG_DEBUG) {
    naos_log("eng_op_http_get: field=%d", field);
  }

  // handle fields
  switch (field) {
    case ENG_HTTP_STATUS: {
      return esp_http_client_get_status_code(eng_http_client);
    }
    case ENG_HTTP_LENGTH: {
      return (int)esp_http_client_get_content_length(eng_http_client);
    }
    case ENG_HTTP_ERRNO: {
      return esp_http_client_get_errno(eng_http_client);
    }
    default:
      return -1;
  }
}

/* runtime */

// https://github.com/bytecodealliance/wasm-micro-runtime/blob/main/doc/export_native_api.md
static NativeSymbol eng_operations[] = {
    {"al_yield", eng_op_yield, "(ii)i", NULL},
    {"al_millis", eng_op_millis, "()I", NULL},
    {"al_clear", eng_op_clear, "(i)", NULL},
    {"al_line", eng_op_line, "(iiiiii)", NULL},
    {"al_rect", eng_op_rect, "(iiiiii)", NULL},
    {"al_write", eng_op_write, "(iiiii*~i)", NULL},
    {"al_draw", eng_op_draw, "(iiiii**)", NULL},
    {"al_gpio", eng_op_gpio, "(ii)i", NULL},
    {"al_i2c", eng_op_i2c, "(i*i*ii)i", NULL},
    {"al_sprite_resolve", eng_op_sprite_resolve, "(*~)i", NULL},
    {"al_sprite_width", eng_op_sprite_width, "(i)i", NULL},
    {"al_sprite_height", eng_op_sprite_height, "(i)i", NULL},
    {"al_sprite_draw", eng_op_sprite_draw, "(iiii)", NULL},
    {"al_sprite_read", eng_op_sprite_read, "(iii)i", NULL},
    {"al_http_new", eng_op_http_new, "()", NULL},
    {"al_http_set", eng_op_http_set, "(ii*~*~)i", NULL},
    {"al_http_run", eng_op_http_run, "(*~*~)i", NULL},
    {"al_http_get", eng_op_http_get, "(i)i", NULL},
};

static void *eng_run_task(void *arg) {
  // get context
  eng_context_t *ctx = arg;

  // prepare variables
  char error_buf[128];
  uint32_t stack_size = 8 * 1024, heap_size = 32 * 1024;

  // prepare runtime init args
  RuntimeInitArgs init_args = {0};

  // configure memory allocator
  init_args.mem_alloc_type = Alloc_With_Allocator;
  init_args.mem_alloc_option.allocator.malloc_func = (void *)eng_malloc;
  init_args.mem_alloc_option.allocator.realloc_func = (void *)eng_realloc;
  init_args.mem_alloc_option.allocator.free_func = (void *)eng_free;

  // register native symbols
  init_args.native_module_name = "env";
  init_args.native_symbols = eng_operations;
  init_args.n_native_symbols = sizeof(eng_operations) / sizeof(NativeSymbol);

  // initialize runtime
  if (!wasm_runtime_full_init(&init_args)) {
    naos_log("eng: init runtime failed");
    return NULL;
  }

  // set log level
  wasm_runtime_set_log_level(WASM_LOG_LEVEL_WARNING);

  // locate main binary
  size_t main_len = 0;
  void *main = eng_bundle_binary(ctx->bundle, "main", &main_len);
  if (!main) {
    naos_log("eng: locating main binary failed");
    return NULL;
  }

  // load application
  wasm_module_t module = wasm_runtime_load(main, main_len, error_buf, sizeof(error_buf));
  if (!module) {
    naos_log("eng: loading WASM module failed: %s", error_buf);
    goto fail;
  }

  // instantiate module
  wasm_module_inst_t module_inst;
  memset(&module_inst, 0, sizeof(wasm_module_inst_t));
  module_inst = wasm_runtime_instantiate(module, stack_size, heap_size, error_buf, sizeof(error_buf));
  if (!module_inst) {
    naos_log("eng: instantiating WASM module failed: %s", error_buf);
    goto fail;
  }

  // create execution environment
  wasm_exec_env_t exec_env;
  memset(&exec_env, 0, sizeof(wasm_exec_env_t));
  exec_env = wasm_runtime_create_exec_env(module_inst, stack_size);
  if (!exec_env) {
    naos_log("eng: creating WASM execution environment failed");
    goto fail;
  }

  // set context
  wasm_runtime_set_user_data(exec_env, ctx);

  // find _start function
  wasm_function_inst_t func = wasm_runtime_lookup_function(module_inst, "_start");
  if (!func) {
    naos_log("eng: looking up _start function failed");
    goto fail;
  }

  // lock graphics
  gfx_begin(false, false);

  // call _start function
  bool ok = wasm_runtime_call_wasm(exec_env, func, 0, NULL);

  // unlock graphics
  gfx_end(false, false);

  // check result
  if (!ok) {
    naos_log("eng: calling _start function failed: %s", wasm_runtime_get_exception(module_inst));
    goto fail;
  }

fail:
  // destroy environment
  if (exec_env) {
    wasm_runtime_destroy_exec_env(exec_env);
  }

  // deinstantiate module
  if (module_inst) {
    wasm_runtime_deinstantiate(module_inst);
  }

  // unload module
  if (module) {
    wasm_runtime_unload(module);
  }

  // destroy runtime
  wasm_runtime_destroy();

  return NULL;
}

bool eng_run() {
  // load bundle
  eng_bundle_t *bundle = eng_bundle_load();
  if (!bundle) {
    return false;
  }

  // prepare context
  eng_context_t ctx = {
      .bundle = bundle,
  };

  // print sections
  naos_log("eng: bundle contains %d sections", ctx.bundle->sections_num);
  for (int i = 0; i < ctx.bundle->sections_num; i++) {
    eng_bundle_section_t *section = &ctx.bundle->sections[i];
    naos_log("[%d]: type=%d name='%s' len=%zu", i, section->type, section->name, section->len);
  }

  // check main binary
  if (!eng_bundle_binary(ctx.bundle, "main", NULL)) {
    naos_log("eng: can't find main binary");
    eng_bundle_free(ctx.bundle);
    return false;
  }

  // clear screen
  gui_cleanup(false);

  // allocate frame buffer
  lv_color_t *frame_buffer = al_calloc(1, LV_CANVAS_BUF_SIZE_TRUE_COLOR(296, 128));

  // create canvas
  ctx.canvas = lv_canvas_create(lv_scr_act());
  lv_canvas_set_buffer(ctx.canvas, frame_buffer, 296, 128, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(ctx.canvas, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_canvas_fill_bg(ctx.canvas, lv_color_white(), LV_OPA_COVER);

  // prepare thread attributes
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  pthread_attr_setstacksize(&attr, 5120);

  // prepare return value
  bool ok = true;

  // create thread
  pthread_t t;
  int res = pthread_create(&t, &attr, eng_run_task, &ctx);
  if (res != 0) {
    naos_log("eng: pthread_create failed: %d", res);
    ok = false;
    // continue
  }

  // join thread
  res = pthread_join(t, NULL);
  if (res != 0) {
    naos_log("eng: pthread_join failed: %d", res);
    ok = false;
    // continue
  }

  // clear screen
  gui_cleanup(false);

  // free buffer
  free(frame_buffer);

  // free bundle
  eng_bundle_free(ctx.bundle);

  return ok;
}
