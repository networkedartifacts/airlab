#include <naos/sys.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <wasm_export.h>
#include <bh_platform.h>
#include <lvgl.h>

#include <al/core.h>

#include "fnt.h"
#include "gfx.h"
#include "gui.h"
#include "internal.h"
#include "lvx.h"
#include "sig.h"

typedef enum {
  ENG_BUNDLE_TYPE_ATTR = 0x00,
  ENG_BUNDLE_TYPE_BINARY = 0x01,
  ENG_BUNDLE_TYPE_SPRITE = 0x02,
} eng_bundle_type_t;

typedef struct {
  eng_bundle_type_t type;
  const char *name;
  size_t len;
  uint8 *data;
} eng_bundle_section_t;

static void *eng_bundle_buf;
static size_t eng_bundle_len;
static uint16 eng_bundle_sections_num;
static eng_bundle_section_t *eng_bundle_sections;
static lv_obj_t *eng_canvas;

/* bundle helpers */

static int eng_bundle_locate(eng_bundle_type_t type, const char *name, eng_bundle_section_t **out) {
  // find matching section
  for (int i = 0; i < eng_bundle_sections_num; i++) {
    if (eng_bundle_sections[i].type == type && strcmp(eng_bundle_sections[i].name, name) == 0) {
      if (out != NULL) {
        *out = &eng_bundle_sections[i];
      }
      return i;
    }
  }

  return -1;
}

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

/* primary operations */

static void eng_op_clear(wasm_exec_env_t _, int c) {
  printf("eng_clear: c=%d\n", c);

  // clear canvas
  lv_canvas_fill_bg(eng_canvas, eng_color(c), LV_OPA_COVER);
}

static void eng_op_rect(wasm_exec_env_t _, int x, int y, int w, int h, int c, int b) {
  printf("eng_rect: x=%d, y=%d, w=%d, h=%d, c=%d, b=%d\n", x, y, w, h, c, b);

  // draw rectangle
  lv_draw_rect_dsc_t rect_dsc;
  lv_draw_rect_dsc_init(&rect_dsc);
  rect_dsc.bg_color = eng_color(c);
  rect_dsc.bg_opa = b > 0 ? LV_OPA_TRANSP : LV_OPA_COVER;
  rect_dsc.border_color = eng_color(c);
  rect_dsc.border_width = b;
  lv_canvas_draw_rect(eng_canvas, x, y, w, h, &rect_dsc);
}

static void eng_op_write(wasm_exec_env_t _, int x, int y, int f, int c, uint8 *text, int text_len) {
  // copy text
  char copy[128];
  if (text_len >= sizeof(copy)) {
    text_len = sizeof(copy) - 1;
  }
  memcpy(copy, text, text_len);
  copy[text_len] = 0;

  printf("eng_write: x=%d, y=%d, f=%d, c=%d, s='%s'\n", x, y, f, c, copy);

  // write text
  lv_draw_label_dsc_t label_dsc;
  lv_draw_label_dsc_init(&label_dsc);
  label_dsc.color = eng_color(c);
  label_dsc.font = eng_font(f);
  lv_canvas_draw_text(eng_canvas, x, y, 296 - x, &label_dsc, copy);
}

static void eng_op_draw(wasm_exec_env_t _, int x, int y, int w, int h, int s, uint8 *i, uint8 *m) {
  printf("eng_draw: x=%d, y=%d, w=%d, h=%d, s=%d\n", x, y, w, h, s);

  if (w <= 0 || h <= 0 || s <= 0) {
    return;
  }

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
  lv_canvas_draw_img(eng_canvas, x, y, &img, &img_draw);
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
  printf("eng_yield\n");

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
  // return time
  return naos_millis();
}

/* sprite functions */

static int eng_op_sprite_resolve(wasm_exec_env_t _, uint8 *name, int name_len) {
  // copy name
  char copy[64];
  if (name_len >= sizeof(copy)) {
    name_len = sizeof(copy) - 1;
  }
  memcpy(copy, name, name_len);
  copy[name_len] = 0;

  printf("eng_op_sprite_resolve: name='%s'\n", copy);

  // locate sprite
  return eng_bundle_locate(ENG_BUNDLE_TYPE_SPRITE, copy, NULL);
}

static int eng_op_sprite_width(wasm_exec_env_t _, int sprite) {
  printf("eng_op_sprite_width: sprite=%d\n", sprite);

  // check sprite
  if (sprite < 0 || sprite >= eng_bundle_sections_num || eng_bundle_sections[sprite].type != ENG_BUNDLE_TYPE_SPRITE) {
    return -1;
  }

  // get width
  uint8 *data = eng_bundle_sections[sprite].data;
  return data[0] | (data[1] << 8);
}

static int eng_op_sprite_height(wasm_exec_env_t _, int sprite) {
  printf("eng_op_sprite_height: sprite=%d\n", sprite);

  // check sprite
  if (sprite < 0 || sprite >= eng_bundle_sections_num || eng_bundle_sections[sprite].type != ENG_BUNDLE_TYPE_SPRITE) {
    return -1;
  }

  // get height
  uint8 *data = eng_bundle_sections[sprite].data;
  return data[2] | (data[3] << 8);
}

static void eng_op_sprite_draw(wasm_exec_env_t e, int sprite, int x, int y, int s) {
  printf("eng_op_sprite_draw: sprite=%d, x=%d, y=%d, s=%d\n", sprite, x, y, s);

  // check sprite
  if (sprite < 0 || sprite >= eng_bundle_sections_num || eng_bundle_sections[sprite].type != ENG_BUNDLE_TYPE_SPRITE) {
    return;
  }

  // get data
  uint8 *data = eng_bundle_sections[sprite].data;
  int w = data[0] | (data[1] << 8);
  int h = data[2] | (data[3] << 8);
  uint8 *img = data + 4;
  uint8 *msk = img + ((w * h + 7) / 8);

  // draw sprite
  eng_op_draw(e, x, y, w, h, s, img, msk);
}

static int eng_op_sprite_read(wasm_exec_env_t _, int sprite, int x, int y) {
  printf("eng_op_sprite_read: sprite=%d, x=%d, y=%d\n", sprite, x, y);

  // check sprite
  if (sprite < 0 || sprite >= eng_bundle_sections_num || eng_bundle_sections[sprite].type != ENG_BUNDLE_TYPE_SPRITE) {
    return -1;
  }

  // get size
  uint8 *data = eng_bundle_sections[sprite].data;
  int w = data[0] | (data[1] << 8);
  int h = data[2] | (data[3] << 8);
  uint8 *img = data + 4;
  uint8 *msk = img + ((w * h + 7) / 8);

  // check parameters
  if (x < 0 || y < 0 || x >= w || y >= h) {
    return -1;
  }

  // compute index
  int idx = y * w + x;

  // test mask
  if (eng_get_bit(msk, idx) == 0) {
    return -1;
  }

  // test image
  if (eng_get_bit(img, idx) != 0) {
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
  printf("eng_gpio: cmd=%d, flags=0x%X\n", cmd, flags);

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
  // perform transfer
  esp_err_t err = al_i2c_transfer(addr, tx, tx_len, rx, rx_len, timeout);

  return err == ESP_OK ? 0 : -1;
}

/* runtime */

// https://github.com/bytecodealliance/wasm-micro-runtime/blob/main/doc/export_native_api.md
static NativeSymbol eng_operations[] = {
    {"al_yield", eng_op_yield, "(ii)i", NULL},
    {"al_millis", eng_op_millis, "()I", NULL},
    {"al_clear", eng_op_clear, "(i)", NULL},
    {"al_rect", eng_op_rect, "(iiiiii)", NULL},
    {"al_write", eng_op_write, "(iiii*~)", NULL},
    {"al_draw", eng_op_draw, "(iiiii**)", NULL},
    {"al_gpio", eng_op_gpio, "(ii)i", NULL},
    {"al_i2c", eng_op_i2c, "(i*i*i*i)i", NULL},
    {"al_sprite_resolve", eng_op_sprite_resolve, "(*~)i", NULL},
    {"al_sprite_width", eng_op_sprite_width, "(i)i", NULL},
    {"al_sprite_height", eng_op_sprite_height, "(i)i", NULL},
    {"al_sprite_draw", eng_op_sprite_draw, "(iiii)", NULL},
    {"al_sprite_read", eng_op_sprite_read, "(iii)i", NULL},
};

void *eng_run_task(void *) {
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
    printf("eng: init runtime failed\n");
    return NULL;
  }

  // set log level
  wasm_runtime_set_log_level(WASM_LOG_LEVEL_VERBOSE);

  // locate main binary
  eng_bundle_section_t *main;
  if (eng_bundle_locate(ENG_BUNDLE_TYPE_BINARY, "main", &main) < 0) {
    printf("eng: locating main binary failed\n");
    return NULL;
  }

  // load application
  wasm_module_t module = wasm_runtime_load(main->data, main->len, error_buf, sizeof(error_buf));
  if (!module) {
    printf("eng: loading WASM module failed: %s\n", error_buf);
    goto fail;
  }

  // instantiate module
  wasm_module_inst_t module_inst;
  memset(&module_inst, 0, sizeof(wasm_module_inst_t));
  module_inst = wasm_runtime_instantiate(module, stack_size, heap_size, error_buf, sizeof(error_buf));
  if (!module_inst) {
    printf("eng: instantiating WASM module failed: %s\n", error_buf);
    goto fail;
  }

  // create execution environment
  wasm_exec_env_t exec_env;
  memset(&exec_env, 0, sizeof(wasm_exec_env_t));
  exec_env = wasm_runtime_create_exec_env(module_inst, stack_size);
  if (!exec_env) {
    printf("eng: creating WASM execution environment failed\n");
    goto fail;
  }

  // find _start function
  wasm_function_inst_t func = wasm_runtime_lookup_function(module_inst, "_start");
  if (!func) {
    printf("eng: looking up _start function failed\n");
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
    printf("eng: calling _start function failed: %s\n", wasm_runtime_get_exception(module_inst));
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

void eng_run(void *bundle_buf, size_t bundle_len) {
  // set bundle
  eng_bundle_buf = bundle_buf;
  eng_bundle_len = bundle_len;

  // check bundle header
  uint8_t version = ((uint8 *)bundle_buf)[4];
  if (bundle_len < 7 || memcmp(bundle_buf, "ALP", 4) != 0 || version != 1) {
    printf("eng: invalid bundle header\n");
    return;
  }

  // parse bundle sections
  size_t offset = 7;
  eng_bundle_sections_num = ((uint8 *)bundle_buf)[5] << 8 | ((uint8 *)bundle_buf)[6];
  eng_bundle_sections = al_calloc(eng_bundle_sections_num, sizeof(eng_bundle_section_t));
  for (int i = 0; i < eng_bundle_sections_num; i++) {
    eng_bundle_section_t *section = &eng_bundle_sections[i];
    if (offset + 5 > bundle_len) {
      printf("eng: truncated bundle\n");
      free(eng_bundle_sections);
      eng_bundle_sections = NULL;
      eng_bundle_sections_num = 0;
      return;
    }
    section->type = ((uint8 *)bundle_buf)[offset];
    offset += 1;
    section->len = ((uint8 *)bundle_buf)[offset] << 24 | ((uint8 *)bundle_buf)[offset + 1] << 16 |
                   ((uint8 *)bundle_buf)[offset + 2] << 8 | ((uint8 *)bundle_buf)[offset + 3];
    offset += 4;
    size_t name_len = strlen((char *)bundle_buf + offset);
    if (offset + name_len + 1 + section->len > bundle_len) {
      printf("eng: truncated bundle\n");
      free(eng_bundle_sections);
      eng_bundle_sections = NULL;
      eng_bundle_sections_num = 0;
      return;
    }
    section->name = "";
    if (name_len > 0) {
      section->name = (char *)bundle_buf + offset;
    }
    offset += name_len + 1;
  }

  // set section data pointers
  for (int i = 0; i < eng_bundle_sections_num; i++) {
    eng_bundle_section_t *section = &eng_bundle_sections[i];
    section->data = (uint8 *)bundle_buf + offset;
    offset += section->len;
  }

  // print sections
  printf("eng: bundle contains %d sections\n", eng_bundle_sections_num);
  for (int i = 0; i < eng_bundle_sections_num; i++) {
    eng_bundle_section_t *section = &eng_bundle_sections[i];
    printf("[%d]: type=%d name='%s' len=%zu\n", i, section->type, section->name, section->len);
  }

  // check main binary
  if (eng_bundle_locate(ENG_BUNDLE_TYPE_BINARY, "main", NULL) < 0) {
    printf("eng: can't find main binary\n");
    free(eng_bundle_sections);
    eng_bundle_sections = NULL;
    eng_bundle_sections_num = 0;
    return;
  }

  // clear screen
  gui_cleanup(false);

  // allocate frame buffer
  lv_color_t *frame_buffer = al_calloc(1, LV_CANVAS_BUF_SIZE_TRUE_COLOR(296, 128));

  // create canvas
  eng_canvas = lv_canvas_create(lv_scr_act());
  lv_canvas_set_buffer(eng_canvas, frame_buffer, 296, 128, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(eng_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_canvas_fill_bg(eng_canvas, lv_color_white(), LV_OPA_COVER);

  // prepare thread attributes
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  pthread_attr_setstacksize(&attr, 5120);

  // create thread
  pthread_t t;
  int res = pthread_create(&t, &attr, eng_run_task, NULL);
  if (res != 0) {
    printf("eng: pthread_create failed: %d\n", res);
    return;
  }

  // join thread
  res = pthread_join(t, NULL);
  if (res != 0) {
    printf("eng: pthread_join failed: %d\n", res);
    return;
  }

  // clear screen
  gui_cleanup(false);

  // free buffer
  free(frame_buffer);

  // free bundle
  free(eng_bundle_sections);
  eng_bundle_sections = NULL;

  // log
  printf("eng: app finished\n");
}
