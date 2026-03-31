#include "../common.h"

#define NUM_ROWS 3
#define ROW_H (AL_H / NUM_ROWS)  // 42
#define PAD_L 8                  // left padding
#define PAD_R 32                 // right padding (unit labels)
#define SCALE_W (AL_W - PAD_L - PAD_R)  // tick ruler width
#define TICK_STEP 7              // pixels between ticks
#define BOX_PAD_X 4              // horizontal padding inside value box
#define BOX_PAD_Y 2              // vertical padding for box within row
#define CHAR_W 9                 // estimated px per char at font 16
#define UNIT_X (AL_W - PAD_R + 6) // unit label left edge

typedef struct {
  al_info_t info;
  const char *unit;
  const char *fmt;
  float min_val;
  float max_val;
} row_t;

static row_t groups[2][NUM_ROWS] = {
    {
        {AL_INFO_SENSOR_CO2, "ppm", "%.0f", 400.0f, 2000.0f},
        {AL_INFO_SENSOR_VOC, "VOC", "%.0f", 0.0f, 500.0f},
        {AL_INFO_SENSOR_TEMPERATURE, "°C", "%.1f", 0.0f, 50.0f},
    },
    {
        {AL_INFO_SENSOR_NOX, "NOx", "%.0f", 0.0f, 200.0f},
        {AL_INFO_SENSOR_HUMIDITY, "% RH", "%.1f", 0.0f, 100.0f},
        {AL_INFO_SENSOR_PRESSURE, "hPa", "%.0f", 900.0f, 1100.0f},
    },
};

int main() {
  // patch temperature
  groups[0][2].unit = al_temp_unit();
  groups[0][2].min_val = al_temp_min();
  groups[0][2].max_val = al_temp_max();

  // read group config
  char grp[4] = {0};
  al_config_get_s("group", grp, sizeof(grp));
  int g = (grp[0] == 'b') ? 1 : 0;
  row_t *rows = groups[g];

  // clear screen
  al_clear(0);

  // draw each row
  for (int ri = 0; ri < NUM_ROWS; ri++) {
    row_t *r = &rows[ri];
    int ry = ri * ROW_H;
    int tick_top = ry + BOX_PAD_Y + 5;
    int tick_bot = ry + ROW_H - BOX_PAD_Y - 5;

    // read value
    float val = al_sensor_value(r->info);
    float denom = r->max_val - r->min_val;
    float ratio = denom > 0.0f ? (val - r->min_val) / denom : 0.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    int val_x = PAD_L + (int)(ratio * SCALE_W);

    // format value string
    char buf[16];
    snprintf(buf, sizeof(buf), r->fmt, val);

    // compute box dimensions
    int box_w = (int)strlen(buf) * CHAR_W + BOX_PAD_X * 2 + 6;
    if (box_w < 22) box_w = 22;
    int box_h = tick_bot - tick_top;
    int box_x = val_x - box_w / 2;
    if (box_x < PAD_L) box_x = PAD_L;
    if (box_x + box_w > PAD_L + SCALE_W) box_x = PAD_L + SCALE_W - box_w;

    // draw ticks across full row
    for (int tx = 0; tx <= SCALE_W; tx += TICK_STEP) {
      int sx = PAD_L + tx;
      if (tx % 5 == 0)
        al_line(sx, tick_top + 5, sx, tick_bot - 5, 1, 1);
      else
        al_line(sx, tick_top + 10, sx, tick_bot - 10, 1, 1);
    }

    // draw inverted value box
    al_rect(box_x, tick_top + 3, box_w, box_h - 6, 1, 0);

    // draw value text in white inside box
    int text_y = tick_top + (box_h - 16) / 2;
    al_write(box_x + box_w / 2, text_y + 1, 0, 16, 0, buf, AL_WRITE_ALIGN_CENTER);

    // draw unit label (font 8, vertically centered, right-aligned)
    int unit_y = ry + (ROW_H - 8) / 2;
    al_write(UNIT_X, unit_y + 1, 0, 8, 1, r->unit, 0);
  }

  return 0;
}
