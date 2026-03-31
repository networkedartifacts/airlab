#include "../common.h"

#define TEXT_FONT 24
#define BOX_PAD_X 5
#define BOX_PAD_Y 4
#define CHAR_W_EST 14  // estimated px per char at font 24

int main() {
  // patch temperature
  al_patch_temp(&al_sensors[AL_SENSOR_TMP]);

  // resolve sensor
  int sidx = al_find_sensor("sensor", "co2");
  al_sensor_t *s = &al_sensors[sidx];

  // format value
  float val = al_sensor_value(s->info);
  char val_str[16];
  snprintf(val_str, sizeof(val_str), s->fmt, val);
  char buf[24];
  snprintf(buf, sizeof(buf), "%s %s", val_str, s->unit);

  // normalize value for fill height
  float ratio = al_normalize(val, s->min_val, s->max_val);
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
