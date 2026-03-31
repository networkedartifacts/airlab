#include "../common.h"

typedef struct {
  al_info_t info;
  const char *unit;
  const char *fmt;
} sensor_t;

static sensor_t sensors[] = {
    {AL_INFO_SENSOR_CO2, "ppm", "%.0f"},       {AL_INFO_SENSOR_TEMPERATURE, "°C", "%.1f"},
    {AL_INFO_SENSOR_HUMIDITY, "% RH", "%.1f"}, {AL_INFO_SENSOR_VOC, "VOC", "%.0f"},
    {AL_INFO_SENSOR_NOX, "NOx", "%.0f"},       {AL_INFO_SENSOR_PRESSURE, "hPa", "%.0f"},
};

static int find_sensor(const char *key, const char *def) {
  char value[32] = {0};
  al_config_get_s(key, value, sizeof(value));
  const char *v = strlen(value) > 0 ? value : def;
  if (strlen(v) == 0) return -1;
  if (strcmp(v, "co2") == 0) return 0;
  if (strcmp(v, "tmp") == 0) return 1;
  if (strcmp(v, "hum") == 0) return 2;
  if (strcmp(v, "voc") == 0) return 3;
  if (strcmp(v, "nox") == 0) return 4;
  if (strcmp(v, "prs") == 0) return 5;
  return -1;
}

static void format_value(char *buf, int buf_len, sensor_t *s) {
  float val = al_sensor_value(s->info);
  char num[32];
  snprintf(num, sizeof(num), s->fmt, val);
  snprintf(buf, buf_len, "%s %s", num, s->unit);
}

int main() {
  // patch temperature
  sensors[1].unit = al_temp_unit();

  // resolve sensors
  int primary = find_sensor("primary", "co2");
  int left = find_sensor("left", "");
  int center = find_sensor("center", "");
  int right = find_sensor("right", "");

  // clear screen
  al_clear(0);

  // draw primary value
  if (primary >= 0) {
    sensor_t *s = &sensors[primary];
    float val = al_sensor_value(s->info);
    char num[32];
    snprintf(num, sizeof(num), s->fmt, val);
    al_write(AL_W / 2, 24, 0, 24, 1, num, AL_WRITE_ALIGN_CENTER);
    al_write(AL_W / 2, 24 + 24 + 8, 0, 16, 1, s->unit, AL_WRITE_ALIGN_CENTER);
  }

  // draw bottom values
  char buf[32];
  int y = AL_H - 16 - 12;
  if (left >= 0) {
    format_value(buf, sizeof(buf), &sensors[left]);
    al_write(12, y, 0, 16, 1, buf, 0);
  }
  if (center >= 0) {
    format_value(buf, sizeof(buf), &sensors[center]);
    al_write(AL_W / 2, y, 0, 16, 1, buf, AL_WRITE_ALIGN_CENTER);
  }
  if (right >= 0) {
    format_value(buf, sizeof(buf), &sensors[right]);
    al_write(AL_W - 12, y, 0, 16, 1, buf, AL_WRITE_ALIGN_RIGHT);
  }

  return 0;
}
