#include <naos.h>
#include <naos/sys.h>
#include <lvgl/src/misc/lv_log.h>

#include "gfx.h"
#include "epd.h"
#include "fnt.h"

#define GFX_WIDTH EPD_HEIGHT
#define GFX_HEIGHT EPD_WIDTH
#define GFX_DEBUG false
#define GFX_TRACE false

// Docs: https://docs.lvgl.io/master/index.html

static naos_mutex_t gfx_mutex;
static lv_disp_draw_buf_t gfx_draw_buffer;
static lv_color_t* gfx_frame_buffer = NULL;
static lv_disp_drv_t gfx_driver;
static lv_disp_t* gfx_display;
static lv_group_t* gfx_group = NULL;
static uint8_t* gfx_frame = NULL;
static lv_theme_t* gfx_theme;
static bool gfx_refresh = false;
static bool gfx_invert = false;
static bool gfx_deferred = false;
static bool gfx_skip = false;
static lv_area_t gfx_flush_area = {0};

static void gfx_task() {
  for (;;) {
    // acquire mutex
    naos_lock(gfx_mutex);

    // update ticks
    lv_tick_inc(50);

    // run timer handler
    lv_timer_handler();

    // release mutex
    naos_unlock(gfx_mutex);

    // await delay
    naos_delay(50);  // 20 Hz
  }
}

static void gfx_flush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* buffer) {
  if (GFX_DEBUG) {
    naos_log("gfx: flush x1=%ld y1=%ld x2=%ld y2=%ld", area->x1, area->y1, area->x2, area->y2);
  }

  // update frame
  for (size_t y = area->y1; y <= area->y2; y++) {
    for (size_t x = area->x1; x <= area->x2; x++) {
      // calculate physical coordinates
      size_t px = y;
      size_t py = EPD_HEIGHT - 1 - x;

      // set physical pixel
      bool black = (*buffer).full == 0;
      epd_set(gfx_frame, px, py, gfx_invert ? !black : black);

      // increment pixel
      buffer++;
    }
  }

  // set or extend flush area
  if (!gfx_deferred) {
    gfx_deferred = true;
    gfx_flush_area = *area;
  } else {
    _lv_area_join(&gfx_flush_area, &gfx_flush_area, area);
  }

  // defer update if not last
  if (!lv_disp_flush_is_last(driver)) {
    lv_disp_flush_ready(driver);
    return;
  }

  // calculate physical area (exclusive)
  uint16_t x1 = gfx_flush_area.y1;
  uint16_t y1 = EPD_HEIGHT - gfx_flush_area.x2;
  uint16_t x2 = gfx_flush_area.y2;
  uint16_t y2 = EPD_HEIGHT - gfx_flush_area.x1;

  // display frame
  if (!gfx_skip) {
    epd_update(gfx_frame, x1, y1, x2, y2, !gfx_refresh);
  }

  // clear flags
  gfx_deferred = false;

  // signal done
  lv_disp_flush_ready(driver);
}

#if GFX_TRACE
static void gfx_log(const char* buf) { printf("%s", buf); }
#else
static void gfx_log(const char* _) {}
#endif

void gfx_init() {
  // create mutex
  gfx_mutex = naos_mutex();

  // allocate buffers
  gfx_frame_buffer = calloc(GFX_WIDTH * GFX_HEIGHT, sizeof(lv_color_t));
  gfx_frame = calloc(EPD_FRAME, sizeof(uint8_t));

  // initialize
  lv_init();

  // initialize buffer
  lv_disp_draw_buf_init(&gfx_draw_buffer, gfx_frame_buffer, NULL, EPD_WIDTH * EPD_HEIGHT);

  // register display driver
  lv_disp_drv_init(&gfx_driver);
  gfx_driver.draw_buf = &gfx_draw_buffer;
  gfx_driver.flush_cb = gfx_flush;
  gfx_driver.hor_res = GFX_WIDTH;
  gfx_driver.ver_res = GFX_HEIGHT;
  gfx_display = lv_disp_drv_register(&gfx_driver);

  // create group
  gfx_group = lv_group_create();

  // initialize theme
  gfx_theme = lv_theme_mono_init(gfx_display, false, &fnt_big);

  // assign theme to display
  lv_disp_set_theme(gfx_display, gfx_theme);

  // register logger
  lv_log_register_print_cb(gfx_log);

  // run task
  naos_run("gfx", 8192, 1, gfx_task);
}

void gfx_begin(bool refresh, bool invert) {
  // acquire mutex
  naos_lock(gfx_mutex);

  // flip frame on inversion change
  if (invert != gfx_invert) {
    for (size_t i = 0; i < EPD_FRAME; i++) {
      gfx_frame[i] ^= UINT8_MAX;
    }
  }

  // set flag
  gfx_refresh = refresh;
  gfx_invert = invert;
}

void gfx_end(bool skip) {
  // set flag
  gfx_skip = skip;

  // release mutex
  naos_unlock(gfx_mutex);
}

lv_group_t* gfx_get_group() {
  // return input
  return gfx_group;
}
