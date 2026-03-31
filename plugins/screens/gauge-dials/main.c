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

int main() {
  // patch temperature
  cm_patch_temp(&cm_sensors[CM_SENSOR_TMP]);

  // get columns
  int cols[NUM_COLS] = {
      cm_find_sensor("col1", "co2"),
      cm_find_sensor("col2", "voc"),
      cm_find_sensor("col3", "tmp"),
  };

  // clear screen
  al_clear(0);

  // draw each column
  for (int i = 0; i < NUM_COLS; i++) {
    // get sensor and value
    cm_sensor_t *s = &cm_sensors[cols[i]];
    float val = al_sensor_value(s->info);

    // compute center
    int cx = COL_PAD + i * COL_W + COL_W / 2;

    // arc outline (opening at bottom)
    al_arc(cx, ARC_CY, ARC_R, ARC_SA, ARC_EA, 1, 2);

    // gauge indicator line on the arc
    float ratio = cm_normalize(val, s->min_val, s->max_val);
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
