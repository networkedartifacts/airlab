#include "../common.h"

#define NUM_COLS 6
#define LINE_H 2
#define GAP 2
#define STRIPE_H (LINE_H + GAP)
#define BAR_TOP 24
#define BAR_BOT 104
#define BAR_AREA_H (BAR_BOT - BAR_TOP)
#define UNIT_Y 108

static cm_sensor_t *cols[NUM_COLS] = {
    &cm_sensors[CM_SENSOR_CO2], &cm_sensors[CM_SENSOR_VOC], &cm_sensors[CM_SENSOR_TMP],
    &cm_sensors[CM_SENSOR_HUM], &cm_sensors[CM_SENSOR_NOX], &cm_sensors[CM_SENSOR_PRS],
};

int main() {
  // patch temperature
  cm_patch_temp(&cm_sensors[CM_SENSOR_TMP]);

  // clear screen and compute layout
  al_clear(0);

  int col_w = AL_W / NUM_COLS - 2;
  int x_off = (AL_W - col_w * NUM_COLS) / 2;
  int bar_w = col_w - 6;
  int max_stripes = BAR_AREA_H / STRIPE_H;

  // draw each sensor column
  for (int i = 0; i < NUM_COLS; i++) {
    // get column
    cm_sensor_t *c = cols[i];
    int x = x_off + i * col_w;

    // read value
    float val = al_sensor_value(c->info);
    char buf[16];
    snprintf(buf, sizeof(buf), c->fmt, val);

    // display unit
    al_write(x + col_w / 2, UNIT_Y + 4, 0, 14, 1, c->unit, AL_WRITE_ALIGN_CENTER);

    // normalize to [0,1] against fixed range
    float ratio = cm_normalize(val, c->min_val, c->max_val);

    // draw horizontal stripes from bottom up
    int fill_stripes = (int)(ratio * max_stripes);
    for (int s = 0; s < fill_stripes; s++) {
      int y = BAR_BOT - (s + 1) * STRIPE_H + GAP;
      al_rect(x + 1, y, bar_w, LINE_H, 1, 0);
    }

    // display value
    int val_y = BAR_BOT - (fill_stripes + 1) * STRIPE_H + GAP - 16;
    al_write(x + col_w / 2, val_y, 0, 16, 1, buf, AL_WRITE_ALIGN_CENTER);
  }

  return 0;
}
