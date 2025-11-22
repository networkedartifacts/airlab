#include <naos.h>
#include <naos/sys.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <wasm_export.h>
#include <lvgl.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/adc.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>

#include <al/core.h>
#include <al/storage.h>
#include <al/buzzer.h>
#include <al/accel.h>
#include <al/power.h>
#include <al/store.h>

#include "com.h"
#include "fnt.h"
#include "gfx.h"
#include "gui.h"
#include "lvx.h"
#include "sig.h"
#include "eng_bundle.h"
#include "hmi.h"

#define ENG_EXEC_DEBUG false

typedef struct {
  eng_bundle_t *bundle;
  const char *binary;
  lv_obj_t *canvas;
  pthread_t thread;
  esp_http_client_config_t http_cfg;
  esp_http_client_handle_t http_client;
} eng_exec_context_t;

static lv_color_t *eng_exec_buffer = NULL;

/* memory helpers */

static void *eng_exec_malloc(unsigned size) {
  // perform alloc 8-byte aligned
  return heap_caps_aligned_alloc(8, size, MALLOC_CAP_SPIRAM);
}

static void *eng_exec_realloc(void *ptr, unsigned size) {
  // TODO: Also 8-byte align?
  // perform realloc
  return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
}

static void eng_exec_free(void *ptr) {
  // perform free
  free(ptr);
}

/* utility helpers */

static lv_color_t eng_exec_color(int c) {
  // determine color
  return c == 1 ? lv_color_black() : lv_color_white();
}

static const lv_font_t *eng_exec_font(int f) {
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

static bool eng_exec_bit(const uint8_t *buf, size_t pos) {
  // get bit
  size_t byte = pos / 8;
  size_t bit = pos % 8;

  return buf[byte] & (1 << bit) ? 1 : 0;
}

static char *eng_exec_mkstr(const uint8_t *buf, int len) {
  // check length
  if (len <= 0) {
    return NULL;
  }

  // copy string
  char *str = eng_exec_malloc(len + 1);
  memcpy(str, buf, len);
  str[len] = 0;

  return str;
}

static bool eng_valid_buf(wasm_exec_env_t env, void *ptr, size_t len, bool allow_null) {
  // check null
  if (ptr == NULL) {
    return allow_null && (len == 0);
  }

  // get module instance
  wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);

  // check native address
  return wasm_runtime_validate_native_addr(inst, ptr, len);
}

/* primary operations */

enum {
  ENG_INFO_BATTERY_LEVEL,
  ENG_INFO_BATTERY_VOLTAGE,
  ENG_INFO_POWER_USB,
  ENG_INGO_POWER_CHARGING,
  ENG_INFO_SENSOR_TEMPERATURE,
  ENG_INFO_SENSOR_HUMIDITY,
  ENG_INFO_SENSOR_CO2,
  ENG_INFO_SENSOR_VOC,
  ENG_INFO_SENSOR_NOX,
  ENG_INFO_SENSOR_PRESSURE,
  ENG_INGO_STORE_SHORT,
  ENG_INGO_STORE_LONG,
  ENG_INFO_ACCEL_FRONT,
  ENG_INFO_ACCEL_ROTATION,
  ENG_INFO_STORAGE_INT,
  ENG_INFO_STORAGE_EXT,
};

static float eng_exec_op_info(wasm_exec_env_t _, int i) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_info: i=%d", i);
  }

  // handle info
  switch (i) {
    case ENG_INFO_BATTERY_LEVEL:
      return al_power_get().bat_level;
    case ENG_INFO_BATTERY_VOLTAGE:
      return al_power_get().bat_voltage;
    case ENG_INFO_POWER_USB:
      return al_power_get().has_usb ? al_power_get().can_fast ? 2.0f : 1.0f : 0.0f;
    case ENG_INGO_POWER_CHARGING:
      return al_power_get().charging ? 1.0f : 0.0f;
    case ENG_INFO_SENSOR_TEMPERATURE:
      return (float)al_store_last().tmp / 100.f;
    case ENG_INFO_SENSOR_HUMIDITY:
      return (float)al_store_last().hum / 100.f;
    case ENG_INFO_SENSOR_CO2:
      return al_store_last().co2;
    case ENG_INFO_SENSOR_VOC:
      return al_store_last().voc;
    case ENG_INFO_SENSOR_NOX:
      return al_store_last().nox;
    case ENG_INFO_SENSOR_PRESSURE:
      return al_store_last().prs;
    case ENG_INGO_STORE_SHORT:
      return (float)al_store_count(AL_STORE_SHORT);
    case ENG_INGO_STORE_LONG:
      return (float)al_store_count(AL_STORE_LONG);
    case ENG_INFO_ACCEL_FRONT:
      return al_accel_get().front ? 1.0f : 0.0f;
    case ENG_INFO_ACCEL_ROTATION:
      return al_accel_get().rotation;
    case ENG_INFO_STORAGE_INT:
      return al_storage_info(AL_STORAGE_INT).usage;
    case ENG_INFO_STORAGE_EXT:
      return al_storage_info(AL_STORAGE_EXT).usage;
    default:
      return -1;
  }
}

enum {
  ENG_CONFIG_BUTTON_REPEAT,
};

static int eng_exec_op_config(wasm_exec_env_t _, int s, int a, int b, int c) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_config: a=%d b=%d c=%d", a, b, c);
  }

  // handle configs
  switch (s) {
    case ENG_CONFIG_BUTTON_REPEAT:
      hmi_set_button_repeat(a);
      return 0;
    default:
      return -1;
  }
}

enum {
  ENG_YIELD_SKIP_FRAME = (1 << 0),
  ENG_YIELD_WAIT_FRAME = (1 << 1),
  ENG_YIELD_INVERT = (1 << 2),
  ENG_YIELD_REFRESH = (1 << 3),
};

enum {
  ENG_YIELD_TIMEOUT = 0,
  ENG_YIELD_ENTER = 1,
  ENG_YIELD_ESCAPE = 2,
  ENG_YIELD_UP = 3,
  ENG_YIELD_DOWN = 4,
  ENG_YIELD_LEFT = 5,
  ENG_YIELD_RIGHT = 6,
};

static int eng_exec_op_yield(wasm_exec_env_t env, int timeout, int flags) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_yield: timeout=%d flags=%d", timeout, flags);
  }

  // unlock graphics
  gfx_end(flags & ENG_YIELD_SKIP_FRAME, flags & ENG_YIELD_WAIT_FRAME);

  // await event or deadline
  sig_event_t event = sig_await(SIG_KEYS | SIG_KILL, timeout);

  // handle kill
  if (event.type == SIG_KILL) {
    // log
    if (ENG_EXEC_DEBUG) {
      naos_log("eng_exec_op_yield: received kill");
    }

    // lock graphics
    gfx_begin(flags & ENG_YIELD_REFRESH, flags & ENG_YIELD_INVERT);

    // clean up the WASM runtime here if needed
    wasm_runtime_set_exception(wasm_runtime_get_module_inst(env), "killed");

    return 0;
  }

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
  gfx_begin(flags & ENG_YIELD_REFRESH, flags & ENG_YIELD_INVERT);

  return ret;
}

static void eng_exec_op_delay(wasm_exec_env_t _, int ms) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_delay: ms=%d", ms);
  }

  // unlock graphics
  gfx_end(false, false);

  // delay
  naos_delay(ms);

  // lock graphics
  gfx_begin(false, false);
}

static int64_t eng_exec_op_millis(wasm_exec_env_t _) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_millis");
  }

  // return time
  return naos_millis();
}

/* interface operations */

static void eng_exec_op_clear(wasm_exec_env_t env, int c) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_clear: c=%d", c);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // clear canvas
  lv_canvas_fill_bg(ctx->canvas, eng_exec_color(c), LV_OPA_COVER);
}

static void eng_exec_op_line(wasm_exec_env_t env, int x1, int y1, int x2, int y2, int c, int b) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_line: x1=%d y1=%d x2=%d y2=%d c=%d b=%d", x1, y1, x2, y2, c, b);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // prepare descriptor
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = eng_exec_color(c);
  line_dsc.width = b;

  // draw line
  lv_point_t points[2] = {{x1, y1}, {x2, y2}};
  lv_canvas_draw_line(ctx->canvas, points, 2, &line_dsc);
}

static void eng_exec_op_rect(wasm_exec_env_t env, int x, int y, int w, int h, int c, int b) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_rect: x=%d y=%d w=%d h=%d c=%d b=%d", x, y, w, h, c, b);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // draw rectangle
  lv_draw_rect_dsc_t rect_dsc;
  lv_draw_rect_dsc_init(&rect_dsc);
  rect_dsc.bg_color = eng_exec_color(c);
  rect_dsc.bg_opa = b > 0 ? LV_OPA_TRANSP : LV_OPA_COVER;
  rect_dsc.border_color = eng_exec_color(c);
  rect_dsc.border_width = b;
  lv_canvas_draw_rect(ctx->canvas, x, y, w, h, &rect_dsc);
}

enum {
  ENG_WRITE_ALIGN_CENTER = (1 << 0),
  ENG_WRITE_ALIGN_RIGHT = (1 << 1),
};

static void eng_exec_op_write(wasm_exec_env_t env, int x, int y, int s, int f, int c, uint8_t *text, int text_len,
                              int flags) {
  // validate buffer
  if (!eng_valid_buf(env, text, text_len, false)) {
    return;
  }

  // copy text
  char copy[128];
  if (text_len >= sizeof(copy)) {
    text_len = sizeof(copy) - 1;
  }
  memcpy(copy, text, text_len);
  copy[text_len] = 0;

  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_write: x=%d y=%d, s=%d f=%d c=%d text='%s' flags=%d", x, y, s, f, c, copy, flags);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // calculate text width
  int w = lv_txt_get_width(copy, text_len, eng_exec_font(f), 0, LV_TEXT_FLAG_NONE);

  // apply alignment
  if (flags & ENG_WRITE_ALIGN_CENTER) {
    x -= w / 2;
  } else if (flags & ENG_WRITE_ALIGN_RIGHT) {
    x -= w;
  }

  // prepare descriptor
  lv_draw_label_dsc_t label_dsc;
  lv_draw_label_dsc_init(&label_dsc);
  label_dsc.color = eng_exec_color(c);
  label_dsc.font = eng_exec_font(f);
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

static void eng_exec_op_draw(wasm_exec_env_t env, int x, int y, int w, int h, int s, int a, const uint8_t *i,
                             const uint8_t *m) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_draw: x=%d y=%d w=%d h=%d s=%d a=%d", x, y, w, h, s, a);
  }

  // check dimensions
  if (w <= 0 || h <= 0 || s <= 0) {
    return;
  }

  // calculate size
  size_t size = ((w * h) + 7) / 8;

  // validate buffers
  if (!eng_valid_buf(env, i, size, false) || !eng_valid_buf(env, m, size, false)) {
    return;
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // prepare sprite
  lvx_sprite_t sprite = {
      .w = w,
      .h = h,
      .s = s,
      .a = a,
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

enum {
  ENG_BEEP_WAIT = (1 << 0),
};

static void eng_exec_op_beep(wasm_exec_env_t _, float freq, int duration, int flags) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_beep: freq=%d duration=%d flags=%d", freq, duration, flags);
  }

  // play beep
  al_buzzer_beep(freq, duration, flags & ENG_BEEP_WAIT);
}

/* IO operations */

enum {
  ENG_GPIO_CONFIG,
  ENG_GPIO_WRITE,        // 0/1
  ENG_GPIO_READ,         // 0/1
  ENG_GPIO_WRITE_PWM,    // 0-4096
  ENG_GPIO_READ_ANALOG,  // 0-4096
};

enum {
  ENG_GPIO_A = (1 << 0),
  ENG_GPIO_B = (1 << 1),
  ENG_GPIO_OUTPUT = (1 << 2),
  ENG_GPIO_INPUT = (1 << 3),
  ENG_GPIO_PULL_UP = (1 << 4),
  ENG_GPIO_PULL_DOWN = (1 << 5),
  ENG_GPIO_ANALOG_INPUT = (1 << 6),
  ENG_GPIO_PWM_OUTPUT = (1 << 7),
};

static int eng_exec_op_gpio(wasm_exec_env_t env, int cmd, int flags, int arg) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_gpio: cmd=%d flags=0x%X arg=%d", cmd, flags, arg);
  }

  // determine GPIO num, LEDC and ADC channels
  gpio_num_t io_num = 0;
  ledc_timer_t pwm_tm = 0;
  ledc_channel_t pwm_ch = 0;
  adc1_channel_t adc_ch = 0;
  if (flags & ENG_GPIO_A) {
    io_num = AL_GPIO_A;
    pwm_tm = LEDC_TIMER_2;
    pwm_ch = LEDC_CHANNEL_6;
    adc_ch = ADC1_CHANNEL_2;
  } else if (flags & ENG_GPIO_B) {
    io_num = AL_GPIO_B;
    pwm_tm = LEDC_TIMER_3;
    pwm_ch = LEDC_CHANNEL_7;
    adc_ch = ADC1_CHANNEL_9;
  } else {
    return -1;
  }

  // handle commands
  switch (cmd) {
    case ENG_GPIO_CONFIG: {
      // reset GPIO
      gpio_reset_pin(io_num);

      // apply configuration
      if (flags & ENG_GPIO_ANALOG_INPUT) {
        // configure attenuation
        esp_err_t err = adc1_config_channel_atten(adc_ch, ADC_ATTEN_DB_12);
        if (err != ESP_OK) {
          return -1;
        }

        return 0;
      } else if (flags & ENG_GPIO_PWM_OUTPUT) {
        // configure LEDC timer
        ledc_timer_config_t tcfg = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .timer_num = pwm_tm,
            .duty_resolution = LEDC_TIMER_12_BIT,
            .freq_hz = arg,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        esp_err_t err = ledc_timer_config(&tcfg);
        if (err != ESP_OK) {
          naos_log("eng_exec_op_gpio: ledc_timer_config failed");
          return -1;
        }

        // configure LEDC channel
        ledc_channel_config_t cfg = {
            .gpio_num = io_num,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .timer_sel = pwm_tm,
            .channel = pwm_ch,
            .duty = 0,
            .hpoint = 0,
        };
        err = ledc_channel_config(&cfg);
        if (err != ESP_OK) {
          naos_log("eng_exec_op_gpio: ledc_timer_config failed");
          return -1;
        }

        return 0;
      } else {
        // determine mode
        gpio_mode_t mode = GPIO_MODE_DISABLE;
        if (flags & ENG_GPIO_INPUT) {
          mode = GPIO_MODE_INPUT;
        } else if (flags & ENG_GPIO_OUTPUT) {
          mode = GPIO_MODE_OUTPUT;
        } else {
          return -1;
        }

        // configure GPIO
        gpio_config_t io_conf = {
            .pin_bit_mask = BIT64(io_num),
            .mode = mode,
            .pull_up_en = flags & ENG_GPIO_PULL_UP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = flags & ENG_GPIO_PULL_DOWN ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
          return -1;
        }

        return 0;
      }
    }

    case ENG_GPIO_WRITE: {
      // set GPIO level
      esp_err_t err = gpio_set_level(io_num, arg == 1 ? 1 : 0);

      return err == ESP_OK ? 0 : -1;
    }
    case ENG_GPIO_READ: {
      // get GPIO level
      int level = gpio_get_level(io_num);

      return level;
    }
    case ENG_GPIO_WRITE_PWM: {
      // set PWM duty
      uint32_t duty = (arg > 4095) ? 4095 : arg;
      esp_err_t err1 = ledc_set_duty(LEDC_LOW_SPEED_MODE, pwm_ch, duty);
      esp_err_t err2 = ledc_update_duty(LEDC_LOW_SPEED_MODE, pwm_ch);
      if (err1 != ESP_OK || err2 != ESP_OK) {
        naos_log("eng_exec_op_gpio: PWM write failed");
        return -1;
      }

      return 0;
    }

    case ENG_GPIO_READ_ANALOG: {
      // get analog value
      int val = adc1_get_raw(adc_ch);

      return val;
    }
    default:
      return -1;
  }
}

static int eng_exec_op_i2c(wasm_exec_env_t env, int addr, uint8_t *tx, int tx_len, uint8_t *rx, int rx_len,
                           int timeout) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_i2c: addr=%d tx=%d rx=%d timeout=%d", addr, tx_len, rx_len, timeout);
  }

  // validate buffers
  if (!eng_valid_buf(env, tx, tx_len, true) || !eng_valid_buf(env, rx, rx_len, true)) {
    return -1;
  }

  // perform transfer
  esp_err_t err = al_i2c_transfer(addr, tx, tx_len, rx, rx_len, timeout);

  return err == ESP_OK ? 0 : -1;
}

/* sprite operations */

static int eng_exec_op_sprite_resolve(wasm_exec_env_t env, uint8_t *name, int name_len) {
  // validate buffers
  if (!eng_valid_buf(env, name, name_len, false)) {
    return -1;
  }

  // copy name
  char copy[64];
  if (name_len >= sizeof(copy)) {
    name_len = sizeof(copy) - 1;
  }
  memcpy(copy, name, name_len);
  copy[name_len] = 0;

  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_sprite_resolve: name='%s'", copy);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // locate sprite
  return eng_bundle_locate(ctx->bundle, ENG_BUNDLE_TYPE_SPRITE, copy, NULL);
}

static int eng_exec_op_sprite_width(wasm_exec_env_t env, int n) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_sprite_width: n=%d", n);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // check sprite
  eng_bundle_section_t *section = &ctx->bundle->sections[n];
  if (n < 0 || n >= ctx->bundle->sections_num || section->type != ENG_BUNDLE_TYPE_SPRITE) {
    return -1;
  }

  // parse sprite
  eng_bundle_sprite_t sp;
  if (!eng_bundle_parse_sprite(&sp, ctx->bundle, section)) {
    naos_log("eng_exec_op_sprite_draw: parsing sprite %d failed", n);
    return -1;
  }

  return sp.width;
}

static int eng_exec_op_sprite_height(wasm_exec_env_t env, int n) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_sprite_height: n=%d", n);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // check sprite
  eng_bundle_section_t *section = &ctx->bundle->sections[n];
  if (n < 0 || n >= ctx->bundle->sections_num || section->type != ENG_BUNDLE_TYPE_SPRITE) {
    return -1;
  }

  // parse sprite
  eng_bundle_sprite_t sp;
  if (!eng_bundle_parse_sprite(&sp, ctx->bundle, section)) {
    naos_log("eng_exec_op_sprite_draw: parsing sprite %d failed", n);
    return -1;
  }

  return sp.height;
}

static void eng_exec_op_sprite_draw(wasm_exec_env_t env, int n, int x, int y, int s, int a) {
  // check scale
  if (s <= 0) {
    return;
  }

  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_sprite_draw: n=%d x=%d y=%d s=%d a=%d", n, x, y, s, a);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // check sprite
  eng_bundle_section_t *section = &ctx->bundle->sections[n];
  if (n < 0 || n >= ctx->bundle->sections_num || section->type != ENG_BUNDLE_TYPE_SPRITE) {
    return;
  }

  // parse sprite
  eng_bundle_sprite_t sp;
  if (!eng_bundle_parse_sprite(&sp, ctx->bundle, section)) {
    naos_log("eng_exec_op_sprite_draw: parsing sprite %d failed", n);
    return;
  }

  // prepare sprite
  lvx_sprite_t sprite = {
      .w = sp.width,
      .h = sp.height,
      .s = s,
      .a = a,
      .img = sp.image,
      .mask = sp.mask,
  };

  // prepare image
  lv_img_dsc_t img = lvx_sprite_img(&sprite);

  // prepare descriptor
  lv_draw_img_dsc_t img_draw;
  lv_draw_img_dsc_init(&img_draw);

  // draw image
  lv_canvas_draw_img(ctx->canvas, x, y, &img, &img_draw);
}

static int eng_exec_op_sprite_read(wasm_exec_env_t env, int sprite, int x, int y) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_sprite_read: sprite=%d x=%d y=%d", sprite, x, y);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // check sprite
  eng_bundle_section_t *section = &ctx->bundle->sections[sprite];
  if (sprite < 0 || sprite >= ctx->bundle->sections_num || section->type != ENG_BUNDLE_TYPE_SPRITE) {
    return -1;
  }

  // parse sprite
  eng_bundle_sprite_t sp;
  if (!eng_bundle_parse_sprite(&sp, ctx->bundle, section)) {
    naos_log("eng_exec_op_sprite_draw: parsing sprite %d failed", sprite);
    return -1;
  }

  // check parameters
  if (x < 0 || y < 0 || x >= sp.width || y >= sp.height) {
    return -1;
  }

  // compute index
  int idx = y * sp.width + x;

  // test mask
  if (eng_exec_bit(sp.mask, idx) == 0) {
    return -1;
  }

  // test image
  if (eng_exec_bit(sp.image, idx) != 0) {
    return 1;
  } else {
    return 0;
  }
}

/* data operations */

static int eng_exec_op_data_get(wasm_exec_env_t env, uint8_t *name, int name_len, uint8_t *buf, int buf_len) {
  // check lengths
  if (name_len <= 0 || buf_len < 0) {
    return -1;
  }

  // validate buffers
  if (!eng_valid_buf(env, name, name_len, false) || !eng_valid_buf(env, buf, buf_len, true)) {
    return -1;
  }

  // copy name
  char *name_copy = eng_exec_mkstr(name, name_len);

  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_data_get: name='%s' len=%d", name_copy, buf_len);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // get bundle name
  const char *bundle_name = eng_bundle_attr(ctx->bundle, "name", NULL);
  if (!bundle_name) {
    eng_exec_free(name_copy);
    return -1;
  }

  // prepare dir
  char dir[32] = {0};
  strcat(dir, "data/");
  strcat(dir, bundle_name);

  // get size
  int size = al_storage_stat(AL_STORAGE_INT, dir, name_copy);
  if (size < 0) {
    eng_exec_free(name_copy);
    return -1;
  }

  // handle size lookup
  if (buf_len <= 0) {
    eng_exec_free(name_copy);
    return size;
  }

  // check size
  if (buf_len != size) {
    eng_exec_free(name_copy);
    return -1;
  }

  // read data
  if (!al_storage_read(AL_STORAGE_INT, dir, name_copy, buf, 0, size)) {
    eng_exec_free(name_copy);
    return -1;
  }

  // free name
  eng_exec_free(name_copy);

  return size;
}

static int eng_exec_op_data_set(wasm_exec_env_t env, uint8_t *name, int name_len, uint8_t *buf, int buf_len) {
  // check lengths
  if (name_len <= 0 || buf_len <= 0) {
    return -1;
  }

  // validate buffers
  if (!eng_valid_buf(env, name, name_len, false) || !eng_valid_buf(env, buf, buf_len, false)) {
    return -1;
  }

  // copy name
  char *name_copy = eng_exec_mkstr(name, name_len);

  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_data_set: name='%s' len=%d", name_copy, buf_len);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // get bundle name
  const char *bundle_name = eng_bundle_attr(ctx->bundle, "name", NULL);
  if (!bundle_name) {
    eng_exec_free(name_copy);
    return -1;
  }

  // prepare dir
  char dir[32] = {0};
  strcat(dir, "data/");
  strcat(dir, bundle_name);

  // TODO: Move to storage write?
  mkdir(AL_STORAGE_INTERNAL "/data", 0777);

  // write data
  al_storage_write(AL_STORAGE_INT, dir, name_copy, buf, 0, buf_len, true);

  // free name
  eng_exec_free(name_copy);

  return buf_len;
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
  // CERT, TLS

  // response
  ENG_HTTP_STATUS,  // int
  ENG_HTTP_LENGTH,  // int
  ENG_HTTP_ERRNO,   // int
};

static esp_err_t eng_exec_http_handler(esp_http_client_event_t *evt) {
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

static void eng_exec_op_http_new(wasm_exec_env_t env) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_http_new");
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // destroy previous client
  if (ctx->http_client) {
    ESP_ERROR_CHECK(esp_http_client_cleanup(ctx->http_client));
  }

  // initialize config
  memset(&ctx->http_cfg, 0, sizeof(esp_http_client_config_t));
  ctx->http_cfg.url = "http://networkedartifacts.com";
  ctx->http_cfg.max_redirection_count = 3;
  ctx->http_cfg.max_authorization_retries = -1;
  ctx->http_cfg.buffer_size = 1024;
  ctx->http_cfg.buffer_size_tx = 1024;
  ctx->http_cfg.event_handler = eng_exec_http_handler;
  ctx->http_cfg.transport_type = HTTP_TRANSPORT_OVER_TCP;
  ctx->http_cfg.crt_bundle_attach = esp_crt_bundle_attach;

  // create client
  ctx->http_client = esp_http_client_init(&ctx->http_cfg);
  if (!ctx->http_client) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }
}

static int eng_exec_op_http_set(wasm_exec_env_t env, int field, int num, uint8_t *str, int str_len, uint8_t *str2,
                                int str2_len) {
  // validate buffers
  if (!eng_valid_buf(env, str, str_len, true) || !eng_valid_buf(env, str2, str2_len, true)) {
    return -1;
  }

  // copy strings
  char *str_copy = eng_exec_mkstr(str, str_len);
  char *str2_copy = eng_exec_mkstr(str2, str2_len);

  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_http_set: field=%d num=%d str='%s'", field, num, str_copy ? str_copy : "");
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // handle fields
  bool ok = true;
  switch (field) {
    case ENG_HTTP_URL:
      esp_http_client_set_url(ctx->http_client, str_copy);  // makes copy
      break;
    case ENG_HTTP_METHOD:
      if (strcmp(str_copy, "GET") == 0) {
        ok = esp_http_client_set_method(ctx->http_client, HTTP_METHOD_GET) == ESP_OK;
      } else if (strcmp(str_copy, "POST") == 0) {
        ok = esp_http_client_set_method(ctx->http_client, HTTP_METHOD_POST) == ESP_OK;
      } else if (strcmp(str_copy, "PUT") == 0) {
        ok = esp_http_client_set_method(ctx->http_client, HTTP_METHOD_PUT) == ESP_OK;
      } else if (strcmp(str_copy, "PATH") == 0) {
        ok = esp_http_client_set_method(ctx->http_client, HTTP_METHOD_PATCH) == ESP_OK;
      } else if (strcmp(str_copy, "DELETE") == 0) {
        ok = esp_http_client_set_method(ctx->http_client, HTTP_METHOD_DELETE) == ESP_OK;
      } else {
        ok = false;
      }
      break;
    case ENG_HTTP_USERNAME:
      ok = esp_http_client_set_username(ctx->http_client, str_copy) == ESP_OK;  // makes copy
      break;
    case ENG_HTTP_PASSWORD:
      ok = esp_http_client_set_password(ctx->http_client, str_copy) == ESP_OK;  // makes copy
      break;
    case ENG_HTTP_HEADER:
      ok = esp_http_client_set_header(ctx->http_client, str_copy, str2_copy) == ESP_OK;  // makes copy
      break;
    case ENG_HTTP_TIMEOUT:
      ok = esp_http_client_set_timeout_ms(ctx->http_client, num) == ESP_OK;
      break;
    default:
      ok = false;
  }

  // free
  eng_exec_free(str_copy);
  eng_exec_free(str2_copy);

  return ok ? 0 : -1;
}

static int eng_exec_op_http_run(wasm_exec_env_t env, uint8_t *req, int req_len, uint8_t *res, int res_len) {
  // validate buffers
  if (!eng_valid_buf(env, req, req_len, true) || !eng_valid_buf(env, res, res_len, true)) {
    return -1;
  }

  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_http_run: req_len=%d res_len=%d", req_len, res_len);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // set request data
  if (req && req_len > 0) {
    esp_http_client_set_post_field(ctx->http_client, (const char *)req, req_len);
  } else {
    esp_http_client_set_post_field(ctx->http_client, NULL, 0);
  }

  // set response buffer
  if (res && res_len > 0) {
    naos_value_t val = {.buf = res, .len = res_len};
    esp_http_client_set_user_data(ctx->http_client, &val);
  } else {
    esp_http_client_set_user_data(ctx->http_client, NULL);
  }

  // perform request
  esp_err_t err = esp_http_client_perform(ctx->http_client);

  return err;
}

static int eng_exec_op_http_get(wasm_exec_env_t env, int field) {
  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_http_get: field=%d", field);
  }

  // get context
  eng_exec_context_t *ctx = wasm_runtime_get_user_data(env);

  // handle fields
  switch (field) {
    case ENG_HTTP_STATUS: {
      return esp_http_client_get_status_code(ctx->http_client);
    }
    case ENG_HTTP_LENGTH: {
      return (int)esp_http_client_get_content_length(ctx->http_client);
    }
    case ENG_HTTP_ERRNO: {
      return esp_http_client_get_errno(ctx->http_client);
    }
    default:
      return -1;
  }
}

/* utils */

static void eng_exec_op_log(wasm_exec_env_t env, uint8_t *msg, int msg_len) {
  // validate buffer
  if (!eng_valid_buf(env, msg, msg_len, false)) {
    return;
  }

  // copy message
  char *cpy = eng_exec_mkstr(msg, msg_len);

  // log
  if (ENG_EXEC_DEBUG) {
    naos_log("eng_exec_op_log: msg='%s'", cpy);
  }

  // log message
  com_log(cpy, msg_len);

  // free copy
  eng_exec_free(cpy);
}

/* runtime */

// https://github.com/bytecodealliance/wasm-micro-runtime/blob/main/doc/export_native_api.md
static NativeSymbol eng_exec_ops[] = {
    {"al_info", eng_exec_op_info, "(i)f", NULL},
    {"al_config", eng_exec_op_config, "(iiii)i", NULL},
    {"al_yield", eng_exec_op_yield, "(ii)i", NULL},
    {"al_delay", eng_exec_op_delay, "(i)", NULL},
    {"al_millis", eng_exec_op_millis, "()I", NULL},
    {"al_clear", eng_exec_op_clear, "(i)", NULL},
    {"al_line", eng_exec_op_line, "(iiiiii)", NULL},
    {"al_rect", eng_exec_op_rect, "(iiiiii)", NULL},
    {"al_write", eng_exec_op_write, "(iiiii*~i)", NULL},
    {"al_beep", eng_exec_op_beep, "(fii)", NULL},
    {"al_draw", eng_exec_op_draw, "(iiiiii**)", NULL},
    {"al_gpio", eng_exec_op_gpio, "(iii)i", NULL},
    {"al_i2c", eng_exec_op_i2c, "(i*i*ii)i", NULL},
    {"al_sprite_resolve", eng_exec_op_sprite_resolve, "(*~)i", NULL},
    {"al_sprite_width", eng_exec_op_sprite_width, "(i)i", NULL},
    {"al_sprite_height", eng_exec_op_sprite_height, "(i)i", NULL},
    {"al_sprite_draw", eng_exec_op_sprite_draw, "(iiiii)", NULL},
    {"al_sprite_read", eng_exec_op_sprite_read, "(iii)i", NULL},
    {"al_data_get", eng_exec_op_data_get, "(*~*~)i", NULL},
    {"al_data_set", eng_exec_op_data_set, "(*~*~)i", NULL},
    {"al_http_new", eng_exec_op_http_new, "()", NULL},
    {"al_http_set", eng_exec_op_http_set, "(ii*~*~)i", NULL},
    {"al_http_run", eng_exec_op_http_run, "(*~*~)i", NULL},
    {"al_http_get", eng_exec_op_http_get, "(i)i", NULL},
    {"al_log", eng_exec_op_log, "(*~)", NULL},
};

static void *eng_exec_task(void *arg) {
  // get context
  eng_exec_context_t *ctx = arg;

  // prepare variables
  char error_buf[128];
  uint32_t stack_size = 8 * 1024, heap_size = 32 * 1024;

  // prepare runtime init args
  RuntimeInitArgs init_args = {0};

  // configure memory allocator
  init_args.mem_alloc_type = Alloc_With_Allocator;
  init_args.mem_alloc_option.allocator.malloc_func = (void *)eng_exec_malloc;
  init_args.mem_alloc_option.allocator.realloc_func = (void *)eng_exec_realloc;
  init_args.mem_alloc_option.allocator.free_func = (void *)eng_exec_free;

  // register native symbols
  init_args.native_module_name = "env";
  init_args.native_symbols = eng_exec_ops;
  init_args.n_native_symbols = sizeof(eng_exec_ops) / sizeof(NativeSymbol);

  // initialize runtime
  if (!wasm_runtime_full_init(&init_args)) {
    naos_log("eng_exec_task: init runtime failed");
    return NULL;
  }

  // set log level
  wasm_runtime_set_log_level(WASM_LOG_LEVEL_WARNING);

  // locate main binary
  size_t main_len = 0;
  void *main = eng_bundle_binary(ctx->bundle, ctx->binary, &main_len);
  if (!main) {
    naos_log("eng_exec_task: locating main binary failed");
    return NULL;
  }

  // load application
  wasm_module_t module = wasm_runtime_load(main, main_len, error_buf, sizeof(error_buf));
  if (!module) {
    naos_log("eng_exec_task: loading WASM module failed: %s", error_buf);
    goto fail;
  }

  // instantiate module
  wasm_module_inst_t module_inst;
  memset(&module_inst, 0, sizeof(wasm_module_inst_t));
  module_inst = wasm_runtime_instantiate(module, stack_size, heap_size, error_buf, sizeof(error_buf));
  if (!module_inst) {
    naos_log("eng_exec_task: instantiating WASM module failed: %s", error_buf);
    goto fail;
  }

  // create execution environment
  wasm_exec_env_t exec_env;
  memset(&exec_env, 0, sizeof(wasm_exec_env_t));
  exec_env = wasm_runtime_create_exec_env(module_inst, stack_size);
  if (!exec_env) {
    naos_log("eng_exec_task: creating WASM execution environment failed");
    goto fail;
  }

  // set context
  wasm_runtime_set_user_data(exec_env, ctx);

  // find _start function
  wasm_function_inst_t func = wasm_runtime_lookup_function(module_inst, "_start");
  if (!func) {
    naos_log("eng_exec_task: looking up _start function failed");
    goto fail;
  }

  // lock graphics
  gfx_begin(false, false);

  // create canvas
  ctx->canvas = lv_canvas_create(lv_scr_act());
  lv_canvas_set_buffer(ctx->canvas, eng_exec_buffer, 296, 128, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(ctx->canvas, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_canvas_fill_bg(ctx->canvas, lv_color_white(), LV_OPA_COVER);

  // call _start function
  bool ok = wasm_runtime_call_wasm(exec_env, func, 0, NULL);

  // unlock graphics
  gfx_end(false, false);

  // reset GPIOs
  gpio_reset_pin(AL_GPIO_A);
  gpio_reset_pin(AL_GPIO_B);

  // destroy HTTP client
  if (ctx->http_client) {
    esp_http_client_cleanup(ctx->http_client);
  }

  // check result
  if (!ok) {
    naos_log("eng_exec_task: calling _start function failed: %s", wasm_runtime_get_exception(module_inst));
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

void *eng_exec_start(eng_bundle_t *bundle, const char *binary) {
  // check binary
  if (!eng_bundle_binary(bundle, binary, NULL)) {
    naos_log("eng_exec_start: binary '%s' not found", binary);
    return NULL;
  }

  // allocate context
  eng_exec_context_t *ctx = eng_exec_malloc(sizeof(eng_exec_context_t));
  if (!ctx) {
    naos_log("eng_exec_start: malloc failed");
    return NULL;
  }

  // clear context
  memset(ctx, 0, sizeof(eng_exec_context_t));

  // set bundle and binary
  ctx->bundle = bundle;
  ctx->binary = binary;

  // clear screen
  gui_cleanup(false);

  // ensure frame buffer
  if (!eng_exec_buffer) {
    eng_exec_buffer = al_calloc(1, LV_CANVAS_BUF_SIZE_TRUE_COLOR(296, 128));
  } else {
    memset(eng_exec_buffer, 0, LV_CANVAS_BUF_SIZE_TRUE_COLOR(296, 128));
  }

  // prepare thread attributes
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  pthread_attr_setstacksize(&attr, 12288);

  // create thread
  int res = pthread_create(&ctx->thread, &attr, eng_exec_task, ctx);
  if (res != 0) {
    naos_log("eng_exec_start: pthread_create failed: %d", res);
    eng_exec_free(ctx);
    return NULL;
  }

  return ctx;
}

void eng_exec_wait(void *ref) {
  // get context
  eng_exec_context_t *ctx = ref;
  if (!ctx) {
    return;
  }

  // join thread
  int res = pthread_join(ctx->thread, NULL);
  if (res != 0) {
    naos_log("eng_exec_wait: pthread_join failed: %d", res);
  }

  // clear screen
  gui_cleanup(false);

  // free context
  eng_exec_free(ctx);
}
