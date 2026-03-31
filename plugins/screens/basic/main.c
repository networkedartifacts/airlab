#include "../common.h"

static void format_value(char *buf, int buf_len, cm_sensor_t *s) {
  float val = al_sensor_value(s->info);
  char num[32];
  snprintf(num, sizeof(num), s->fmt, val);
  snprintf(buf, buf_len, "%s %s", num, s->unit);
}

int main() {
  // patch temperature
  cm_patch_temp(&cm_sensors[CM_SENSOR_TMP]);

  // resolve sensors
  int primary = cm_find_sensor("primary", "co2");
  int left = cm_find_sensor("left", "");
  int center = cm_find_sensor("center", "");
  int right = cm_find_sensor("right", "");

  // clear screen
  al_clear(0);

  // draw primary value
  if (primary >= 0) {
    cm_sensor_t *s = &cm_sensors[primary];
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
    format_value(buf, sizeof(buf), &cm_sensors[left]);
    al_write(12, y, 0, 16, 1, buf, 0);
  }
  if (center >= 0) {
    format_value(buf, sizeof(buf), &cm_sensors[center]);
    al_write(AL_W / 2, y, 0, 16, 1, buf, AL_WRITE_ALIGN_CENTER);
  }
  if (right >= 0) {
    format_value(buf, sizeof(buf), &cm_sensors[right]);
    al_write(AL_W - 12, y, 0, 16, 1, buf, AL_WRITE_ALIGN_RIGHT);
  }

  return 0;
}
