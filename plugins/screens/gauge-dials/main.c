#include "../common.h"

#include <math.h>

#define NUM_COLS 3
#define COL_PAD 10
#define COL_W ((AL_W - 2 * COL_PAD) / NUM_COLS)
#define ARC_CY 70
#define ARC_R 42
#define ARC_SA 130
#define ARC_EA 50
#define GAUGE_LEN 10

typedef struct {
  al_info_t info;
  const char *unit;
  const char *fmt;
  float min_val;
  float max_val;
} sensor_t;

static sensor_t sensors[] = {
    {AL_INFO_SENSOR_CO2, "ppm", "%.0f", 400.0f, 2000.0f},    {AL_INFO_SENSOR_TEMPERATURE, "°C", "%.1f", 0.0f, 50.0f},
    {AL_INFO_SENSOR_HUMIDITY, "% RH", "%.1f", 0.0f, 100.0f}, {AL_INFO_SENSOR_VOC, "VOC", "%.0f", 0.0f, 500.0f},
    {AL_INFO_SENSOR_NOX, "NOx", "%.0f", 0.0f, 200.0f},       {AL_INFO_SENSOR_PRESSURE, "hPa", "%.0f", 900.0f, 1100.0f},
};

static int find_sensor(const char *key, const char *def) {
  char value[32] = {0};
  al_config_get_s(key, value, sizeof(value));
  const char *v = strlen(value) > 0 ? value : def;
  if (strcmp(v, "co2") == 0) return 0;
  if (strcmp(v, "tmp") == 0) return 1;
  if (strcmp(v, "hum") == 0) return 2;
  if (strcmp(v, "voc") == 0) return 3;
  if (strcmp(v, "nox") == 0) return 4;
  if (strcmp(v, "prs") == 0) return 5;
  return 0;
}

int main() {
  // patch temperature
  sensors[1].unit = al_temp_unit();
  sensors[1].min_val = al_temp_min();
  sensors[1].max_val = al_temp_max();

  // get columns
  int cols[NUM_COLS] = {
      find_sensor("col1", "co2"),
      find_sensor("col2", "voc"),
      find_sensor("col3", "tmp"),
  };

  // clear screen
  al_clear(0);

  // draw each column
  for (int i = 0; i < NUM_COLS; i++) {
    // get sensor and value
    sensor_t *s = &sensors[cols[i]];
    float val = al_sensor_value(s->info);

    // compute center
    int cx = COL_PAD + i * COL_W + COL_W / 2;

    // arc outline (opening at bottom)
    al_arc(cx, ARC_CY, ARC_R, ARC_SA, ARC_EA, 1, 2);

    // gauge indicator line on the arc
    float denom = s->max_val - s->min_val;
    float ratio = denom > 0.0f ? (val - s->min_val) / denom : 0.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    float deg = (ARC_SA + 90.f) + ratio * (360.f - (ARC_SA - ARC_EA));
    if (deg >= 360.0f) deg -= 360.0f;
    float rad = deg * (M_PI / 180.0f);
    int gx1 = cx + (int)roundf((ARC_R - GAUGE_LEN) * sinf(rad));
    int gy1 = ARC_CY - (int)roundf((ARC_R - GAUGE_LEN) * cosf(rad));
    int gx2 = cx + (int)roundf(ARC_R * sinf(rad));
    int gy2 = ARC_CY - (int)roundf(ARC_R * cosf(rad));
    al_line(gx1, gy1, gx2, gy2, 1, 2);

    // unit label below arc opening
    al_write(cx, ARC_CY + ARC_R - 12, 0, 8, 1, s->unit, AL_WRITE_ALIGN_CENTER);

    // value inside arc
    char buf[16];
    snprintf(buf, sizeof(buf), s->fmt, val);
    al_write(cx, ARC_CY - 12, 0, 24, 1, buf, AL_WRITE_ALIGN_CENTER);
  }

  return 0;
}
