#ifndef LVX_H
#define LVX_H

#include <lvgl.h>

#include "sig.h"

/* Wheel */

typedef struct {
  const char *format;
  uint16_t value;
  uint16_t min;
  uint16_t max;
  bool fixed;
  // ---
  lv_obj_t *_col;
  lv_obj_t *_up;
  lv_obj_t *_lbl;
  lv_obj_t *_down;
} lvx_wheel_t;

void lvx_wheel_create(lvx_wheel_t *wheel, lv_obj_t *parent);

/* Sign */

typedef struct {
  const char *title;
  const char *text;
  lv_align_t align;
  lv_coord_t offset;
  // ---
  lv_obj_t *_row;
  lv_obj_t *_title;
  lv_obj_t *_text;
} lvx_sign_t;

void lvx_sign_create(lvx_sign_t *sign, lv_obj_t *parent);

/* Bar */

typedef struct {
  const char *time;
  const char *mark;
  const char *value;
  // ---
  lv_obj_t *_time;
  lv_obj_t *_pwr;
  lv_obj_t *_rec;
  lv_obj_t *_mrk;
  lv_obj_t *_val;
  lv_obj_t *_ar1;
  lv_obj_t *_ar2;
} lvx_bar_t;

void lvx_bar_create(lvx_bar_t *bar, lv_obj_t *parent);
void lvx_bar_update(lvx_bar_t *bar);

/* Bubble */

typedef struct {
  const char *text;
  // ---
  lv_obj_t *_frame;
  lv_obj_t *_label;
} lvx_bubble_t;

void lvx_bubble_create(lvx_bubble_t *bubble, lv_obj_t *parent);
void lvx_bubble_update(lvx_bubble_t *bubble);

/* Canvas */

#define LVX_CHART_SIZE 72

typedef struct {
  float range;
  float *values;
  uint8_t *marks;
  bool arrows;
  int64_t offset;  // ms since 1970
  int32_t start;   // relative to offset
  int32_t end;     // relative to offset
  int32_t stop;    // relative to offset
  bool cursor;
  int index;
} lvx_chart_data_t;

void lvx_chart_draw(lv_obj_t *canvas, lvx_chart_data_t data);

/* Helpers */

bool lvx_handle(sig_event_t event, bool focus);

void lvx_log_event(lv_event_t *event);

void lvx_style_set_pad(lv_style_t *style, lv_coord_t top, lv_coord_t bottom, lv_coord_t left, lv_coord_t right);

#endif  // LVX_H
