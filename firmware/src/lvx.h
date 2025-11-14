#ifndef LVX_H
#define LVX_H

#include <lvgl.h>

#include "sig.h"

/* Formatting */

const char *lvx_fmt(const char *fmt, ...);
const char *lvx_truncate(const char *str, size_t max_len);

/* Wheel */

typedef struct {
  const char *format;
  uint16_t value;
  uint16_t min;
  uint16_t step;
  uint16_t max;
  bool fixed;
  // ---
  lv_obj_t *_col;
  lv_obj_t *_up;
  lv_obj_t *_lbl;
  lv_obj_t *_down;
} lvx_wheel_t;

void lvx_wheel_create(lvx_wheel_t *wheel, lv_obj_t *parent);
void lvx_wheel_focus(lvx_wheel_t *wheel, bool focused);
void lvx_wheel_update(lvx_wheel_t *wheel, int change);

/* Wheel Group */

void lvx_wheel_group_update(lvx_wheel_t **wheels, int num_wheels, sig_event_t event, int *selected);

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

/* Status */

typedef struct {
  lv_obj_t *row;
  lv_obj_t *pwr;
  lv_obj_t *rec;
  lv_obj_t *net;
  lv_obj_t *ble;
} lvx_status_t;

void lvx_status_create(lvx_status_t *status, lv_obj_t *parent);
void lvx_status_update(lvx_status_t *status);

/* Bar */

typedef struct {
  const char *time;
  const char *mark;
  const char *value;
  // ---
  lv_obj_t *_time;
  lvx_status_t _status;
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
  lv_obj_t *canvas;
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
} lvx_chart_t;

void lvx_chart_draw(lvx_chart_t chart);

/* Helpers */

void lvx_log_event(lv_event_t *event);

void lvx_style_set_pad(lv_style_t *style, lv_coord_t top, lv_coord_t bottom, lv_coord_t left, lv_coord_t right);

/* Bitmap Decoder */

typedef struct {
  uint16_t w, h, s, a;
  const uint8_t *img;
  const uint8_t *mask;
} lvx_sprite_t;

lv_img_dsc_t lvx_sprite_img(lvx_sprite_t *sprite);

/* General */

void lvx_init();

#endif  // LVX_H
