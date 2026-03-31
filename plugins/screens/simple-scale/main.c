#include "../common.h"

#define SCALE_Y 84
#define MAJOR_TICK_Y (SCALE_Y + 25)
#define MINOR_TICK_Y (SCALE_Y + 35)
#define LABEL_Y 95
#define LINE_WIDTH 2

static float major_steps[AL_SENSOR_COUNT] = {500.0f, 10.0f, 25.0f, 100.0f, 50.0f, 50.0f};
static float minor_steps[AL_SENSOR_COUNT] = {100.0f, 5.0f, 5.0f, 50.0f, 10.0f, 10.0f};

static int val_to_x(float val, float min_val, float max_val) {
  return (int)(al_normalize(val, min_val, max_val) * (AL_W - 1));
}

int main() {
  // patch temperature
  al_patch_temp(&al_sensors[AL_SENSOR_TMP]);

  // resolve sensor and read value
  int sidx = al_find_sensor("sensor", "co2");
  al_sensor_t *s = &al_sensors[sidx];
  float val = al_sensor_value(s->info);
  float major_step = major_steps[sidx];
  float minor_step = minor_steps[sidx];

  // clear screen
  al_clear(0);

  // unit label (small, top center)
  al_write(AL_W / 2, 18, 0, 8, 1, s->unit, AL_WRITE_ALIGN_CENTER);

  // primary value (large, centered)
  char buf[16];
  snprintf(buf, sizeof(buf), s->fmt, val);
  al_write(AL_W / 2, 40, 0, 24, 1, buf, AL_WRITE_ALIGN_CENTER);

  // downward triangle marker
  int marker_x = AL_W / 2;
  int tri_y = 75;
  for (int row = 0; row < 6; row++) {
    int w = 11 - row * 2;
    al_rect(marker_x - w / 2, tri_y + row, w, 1, 1, 0);
  }

  // minor ticks (increment adjusted per sensor for visual density)
  int scale_x = val_to_x(val, s->min_val, s->max_val);
  float minor_increment = minor_step / 2;
  if (strcmp(s->unit, "% RH") == 0) {
    minor_increment = minor_step / 2.4;
  } else if (strcmp(s->unit, "°C") == 0 || strcmp(s->unit, "°F") == 0) {
    minor_increment = minor_step / 4;
  } else if (strcmp(s->unit, "VOC") == 0) {
    minor_increment = minor_step / 4;
  }
  for (float v = s->min_val; v <= s->max_val + 0.001f; v += minor_increment) {
    int tx = val_to_x(v, s->min_val, s->max_val) + AL_W / 2 - scale_x;
    al_line(tx, MINOR_TICK_Y, tx, AL_H - 1, 1, LINE_WIDTH);
  }

  // major ticks + labels (skip min_val label)
  for (float v = s->min_val + major_step; v <= s->max_val + 0.001f; v += major_step) {
    int tx = val_to_x(v, s->min_val, s->max_val) + AL_W / 2 - scale_x;
    al_line(tx, MAJOR_TICK_Y, tx, AL_H - 1, 1, LINE_WIDTH);
    char label[12];
    snprintf(label, sizeof(label), "%.0f", v);
    al_write(tx, LABEL_Y, 0, 8, 1, label, AL_WRITE_ALIGN_CENTER);
  }

  return 0;
}
