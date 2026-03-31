#include "../common.h"

#define TEXT_FONT 24
#define BOX_PAD_X 5
#define BOX_PAD_Y 4
#define CHAR_W_EST 14  // estimated px per char at font 24

typedef struct {
  al_info_t info;
  const char *unit;
  const char *fmt;
  float min_val;
  float max_val;
} sensor_t;

static sensor_t sensors[] = {
    {AL_INFO_SENSOR_CO2, "ppm", "%.0f", 400.0f, 2000.0f}, {AL_INFO_SENSOR_TEMPERATURE, "°C", "%.1f", 0.0f, 50.0f},
    {AL_INFO_SENSOR_HUMIDITY, "% RH", "%.1f", 0.0f, 100.0f}, {AL_INFO_SENSOR_VOC, "VOC", "%.0f", 0.0f, 500.0f},
    {AL_INFO_SENSOR_NOX, "NOx", "%.0f", 0.0f, 200.0f},    {AL_INFO_SENSOR_PRESSURE, "hPa", "%.0f", 900.0f, 1100.0f},
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

  // resolve sensor
  int sidx = find_sensor("sensor", "co2");
  sensor_t *s = &sensors[sidx];

  // format value
  float val = al_sensor_value(s->info);
  char val_str[16];
  snprintf(val_str, sizeof(val_str), s->fmt, val);
  char buf[24];
  snprintf(buf, sizeof(buf), "%s %s", val_str, s->unit);

  // normalize value for fill height
  float denom = s->max_val - s->min_val;
  float ratio = denom > 0.0f ? (val - s->min_val) / denom : 0.0f;
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  int fill_h = (int)(ratio * AL_H);

  // tight white box dimensions
  int box_w = (int)strlen(buf) * CHAR_W_EST + BOX_PAD_X * 2;
  int box_h = TEXT_FONT + BOX_PAD_Y * 2;
  int box_x = (AL_W - box_w) / 2;
  int box_y = (AL_H - box_h) / 2;

  // white background
  al_clear(0);

  // black fill rising from bottom
  if (fill_h > 0) {
    al_rect(0, AL_H - fill_h, AL_W, fill_h, 1, 0);
  }

  // tight white box behind text
  al_rect(box_x, box_y, box_w, box_h, 0, 0);

  // black text centered in box
  al_write(AL_W / 2, box_y + BOX_PAD_Y, 0, TEXT_FONT, 1, buf, AL_WRITE_ALIGN_CENTER);

  return 0;
}
