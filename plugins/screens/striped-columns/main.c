#include "../common.h"

#define NUM_COLS 6
#define LINE_H 2
#define GAP 2
#define STRIPE_H (LINE_H + GAP)
#define BAR_TOP 24
#define BAR_BOT 104
#define BAR_AREA_H (BAR_BOT - BAR_TOP)
#define UNIT_Y 108

typedef struct {
  al_info_t info;
  const char *unit;
  const char *fmt;
  float min_val;
  float max_val;
} col_t;

static col_t cols[NUM_COLS] = {
    {AL_INFO_SENSOR_CO2, "ppm", "%.0f", 400.0f, 2000.0f},    {AL_INFO_SENSOR_VOC, "VOC", "%.0f", 0.0f, 500.0f},
    {AL_INFO_SENSOR_TEMPERATURE, "°C", "%.1f", 0.0f, 50.0f}, {AL_INFO_SENSOR_HUMIDITY, "% RH", "%.1f", 0.0f, 100.0f},
    {AL_INFO_SENSOR_NOX, "NOx", "%.0f", 0.0f, 200.0f},       {AL_INFO_SENSOR_PRESSURE, "hPa", "%.0f", 900.0f, 1100.0f},
};

int main() {
  // patch temperature
  cols[2].unit = al_temp_unit();
  cols[2].min_val = al_temp_min();
  cols[2].max_val = al_temp_max();

  // clear screen and compute layout
  al_clear(0);

  int col_w = AL_W / NUM_COLS - 2;
  int x_off = (AL_W - col_w * NUM_COLS) / 2;
  int bar_w = col_w - 6;
  int max_stripes = BAR_AREA_H / STRIPE_H;

  // draw each sensor column
  for (int i = 0; i < NUM_COLS; i++) {
    // get column
    col_t *c = &cols[i];
    int x = x_off + i * col_w;

    // read value
    float val = al_sensor_value(c->info);
    char buf[16];
    snprintf(buf, sizeof(buf), c->fmt, val);

    // display unit
    al_write(x + col_w / 2, UNIT_Y + 4, 0, 14, 1, c->unit, AL_WRITE_ALIGN_CENTER);

    // normalize to [0,1] against fixed range
    float denom = c->max_val - c->min_val;
    float ratio = denom > 0.0f ? (val - c->min_val) / denom : 0.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

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
