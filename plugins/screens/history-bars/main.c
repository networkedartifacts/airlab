#include "../common.h"

#define BAR_N 59
#define BAR_W 4
#define BAR_GAP 1
#define BAR_STEP (BAR_W + BAR_GAP)
#define BAR_AREA_H 36
#define SPAN_MS (5 * 60 * 1000)  // 5 minutes

int main() {
  // patch temperature
  al_patch_temp(&al_sensors[AL_SENSOR_TMP]);

  // resolve sensor
  int sidx = al_find_sensor("sensor", "co2");
  al_sensor_t *s = &al_sensors[sidx];

  // query historical samples
  int store_stop = al_store_info(AL_STORE_INFO_LENGTH);
  int resolution = SPAN_MS / BAR_N;
  int first = store_stop - SPAN_MS;
  int count = BAR_N;
  if (first < 0) {
    first = 0;
    count = store_stop / resolution;
    if (count < 1 && store_stop > 0) count = 1;
    if (count > BAR_N) count = BAR_N;
  }
  float values[BAR_N] = {0};
  int n = (resolution > 0 && count > 0) ? al_store_query(s->field, values, count, first, resolution) : 0;

  // convert stored temperature values
  if (s->info == AL_INFO_SENSOR_TEMPERATURE) {
    for (int i = 0; i < n; i++) {
      values[i] = al_temp_convert(values[i]);
    }
  }

  // current live value
  float val = al_sensor_value(s->info);

  // min/max for bar normalization
  float min_val = 0, max_val = 0;
  if (n > 0) {
    min_val = values[0];
    max_val = values[0];
    for (int i = 1; i < n; i++) {
      if (values[i] < min_val) min_val = values[i];
      if (values[i] > max_val) max_val = values[i];
    }
  }
  float range = max_val - min_val;
  if (range < 1.0f) range = 1.0f;

  // clear screen
  al_clear(0);

  // primary value centered in upper area
  char num[32], display[48];
  snprintf(num, sizeof(num), s->fmt, val);
  snprintf(display, sizeof(display), "%s %s", num, s->unit);
  al_write(AL_W / 2, (AL_H - BAR_AREA_H - 24) / 2, 0, 24, 1, display, AL_WRITE_ALIGN_CENTER);

  // bar chart at bottom, newest bar on the right
  int offset = (BAR_N - n) * BAR_STEP;
  for (int i = 0; i < n; i++) {
    int bar_h = (int)((values[i] - min_val) / range * (BAR_AREA_H - 2)) + 2;
    int x = offset + i * BAR_STEP;
    al_rect(x, AL_H - bar_h, BAR_W, bar_h, 1, 0);
  }

  return 0;
}
