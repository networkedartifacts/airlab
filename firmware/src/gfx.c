#include <naos.h>
#include <naos/sys.h>
#include <esp_err.h>

#include <al/core.h>
#include <al/epd.h>
#include <al/storage.h>

#include "gfx.h"
#include "fnt.h"
#include "lvx.h"

#define GFX_WIDTH AL_EPD_HEIGHT
#define GFX_HEIGHT AL_EPD_WIDTH
#define GFX_DEBUG false

// Docs: https://docs.lvgl.io/master/index.html

static naos_mutex_t gfx_mutex;
static naos_signal_t gfx_signal;
static lv_disp_draw_buf_t gfx_buffer;
static lv_color_t* gfx_frame = NULL;
static lv_disp_drv_t gfx_driver;
static lv_disp_t* gfx_display;
static uint8_t* gfx_bitmap = NULL;
static lv_theme_t* gfx_theme;
static bool gfx_refresh = false;
static bool gfx_invert = false;
static bool gfx_skip = false;
static bool gfx_record = false;

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
      size_t py = AL_EPD_HEIGHT - x - 1;

      // set physical pixel
      bool black = (*buffer).full == 0;
      al_epd_set(gfx_bitmap, px, py, gfx_invert ? !black : black);

      // increment pixel
      buffer++;
    }
  }

  // defer update if not last
  if (!lv_disp_flush_is_last(driver)) {
    lv_disp_flush_ready(driver);
    return;
  }

  // check if frame should be skipped
  if (!gfx_skip) {
    // display frame
    al_epd_update(gfx_bitmap, !gfx_refresh);
    if (GFX_DEBUG) {
      naos_log("gfx: updated partial=%d", !gfx_refresh);
    }

    // record screen, if enabled
    if (gfx_record) {
      char name[32];
      snprintf(name, sizeof(name), "screen-%llu.bin", naos_millis());
      al_storage_write(AL_STORAGE_EXT, "dump", name, gfx_bitmap, 0, AL_EPD_FRAME, true);
    }
  }

  // signal done
  lv_disp_flush_ready(driver);

  // trigger signal
  naos_trigger(gfx_signal, 1, false);
}

static naos_param_t gfx_params[] = {
    {.name = "gfx-record", .type = NAOS_BOOL, .sync_b = &gfx_record, .mode = NAOS_VOLATILE},
};

void gfx_init(bool reset) {
  // register params
  for (size_t i = 0; i < NAOS_COUNT(gfx_params); i++) {
    naos_register(gfx_params);
  }

  // create mutex and signal
  gfx_mutex = naos_mutex();
  gfx_signal = naos_signal();

  // allocate buffers
  gfx_frame = al_calloc(GFX_WIDTH * GFX_HEIGHT, sizeof(lv_color_t));
  gfx_bitmap = al_calloc(AL_EPD_FRAME, sizeof(uint8_t));

  if (gfx_frame == NULL) {
    ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
  } else if (gfx_bitmap == NULL) {
    ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
  }

  // initialize
  lv_init();
  lvx_init();

  // initialize buffer
  lv_disp_draw_buf_init(&gfx_buffer, gfx_frame, NULL, AL_EPD_WIDTH * AL_EPD_HEIGHT);

  // register display driver
  lv_disp_drv_init(&gfx_driver);
  gfx_driver.draw_buf = &gfx_buffer;
  gfx_driver.flush_cb = gfx_flush;
  gfx_driver.hor_res = GFX_WIDTH;
  gfx_driver.ver_res = GFX_HEIGHT;
  gfx_driver.sw_rotate = 1;
  gfx_display = lv_disp_drv_register(&gfx_driver);

  // initialize theme
  gfx_theme = lv_theme_mono_init(gfx_display, false, &fnt_16);

  // assign theme to display
  lv_disp_set_theme(gfx_display, gfx_theme);

  // skip initial draw, if not reset
  gfx_skip = !reset;

  // clear screen on reset
  if (reset) {
    al_epd_update(gfx_bitmap, false);
  }

  // run task
  naos_run("gfx", 4096, 1, gfx_task);
}

void gfx_begin(bool refresh, bool invert) {
  if (GFX_DEBUG) {
    naos_log("gfx: begin refresh=%d invert=%d", refresh, invert);
  }

  // acquire mutex
  naos_lock(gfx_mutex);

  // flip frame on inversion change
  if (invert != gfx_invert) {
    for (size_t i = 0; i < AL_EPD_FRAME; i++) {
      gfx_bitmap[i] ^= UINT8_MAX;
    }
  }

  // set flag
  gfx_refresh = refresh;
  gfx_invert = invert;

  // clear signal
  naos_trigger(gfx_signal, 1, true);
}

void gfx_end(bool skip, bool wait) {
  if (GFX_DEBUG) {
    naos_log("gfx: end skip=%d wait=%d", skip, wait);
  }

  // set flag
  gfx_skip = skip;

  // release mutex
  naos_unlock(gfx_mutex);

  // await signal
  if (wait) {
    naos_await(gfx_signal, 1, false, -1);
    if (GFX_DEBUG) {
      naos_log("gfx: end done");
    }
  }
}
