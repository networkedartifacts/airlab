#include "../../al.h"

#define GRAPH_X 4
#define GRAPH_Y 4
#define GRAPH_W (AL_W - GRAPH_X * 2)
#define GRAPH_H (AL_H - GRAPH_Y - 20)
#define BAR_W 2
#define BAR_S 2
#define BAR_N 72

#define NUM_FIELDS 6
#define NUM_SPANS 6

typedef struct {
  const char *name;
  al_store_query_field_t field;
  const char *unit;
  const char *fmt;
} field_info_t;

static const field_info_t field_table[NUM_FIELDS] = {
    {"CO2", AL_STORE_QUERY_CO2, "ppm", "%.0f"},
    {"Temp", AL_STORE_QUERY_TMP,
     "\xc2\xb0"
     "C",
     "%.1f"},
    {"Hum", AL_STORE_QUERY_HUM, "%", "%.1f"},
    {"VOC", AL_STORE_QUERY_VOC, "VOC", "%.0f"},
    {"NOx", AL_STORE_QUERY_NOX, "NOx", "%.0f"},
    {"Prs", AL_STORE_QUERY_PRS, "hPa", "%.0f"},
};

static const int span_table[NUM_SPANS] = {5, 15, 60, 180, 720, 1440};

static void draw(int field_idx, int span_idx) {
  // get field and span
  const field_info_t *fi = &field_table[field_idx];
  int span = span_table[span_idx];

  // determine time range
  int store_length = (int)al_store_info(AL_STORE_INFO_LENGTH);
  int span_ms = span * 60 * 1000;
  int resolution = span_ms / BAR_N;
  int first = store_length - span_ms;
  int count = BAR_N;
  if (first < 0) {
    first = 0;
    count = store_length / resolution;
    if (count < 1 && store_length > 0) count = 1;
    if (count > BAR_N) count = BAR_N;
  }

  // query samples
  float values[BAR_N] = {0};
  int n = (resolution > 0 && count > 0) ? al_store_query(fi->field, values, count, first, resolution) : 0;

  // find min/max
  float min = 0;
  float max = 0;
  if (n > 0) {
    min = values[0];
    max = values[0];
    for (int i = 1; i < n; i++) {
      if (values[i] < min) min = values[i];
      if (values[i] > max) max = values[i];
    }
  }

  // keep original min/max for labels
  float data_min = min;
  float data_max = max;

  // ensure range
  if (max - min < 1.0f) {
    min -= 0.5f;
    max += 0.5f;
  }

  // add margin
  float range = max - min;
  min -= range * 0.1f;
  max += range * 0.1f;
  range = max - min;

  // clear and draw bars
  al_clear(0);
  char buf[64];
  if (n > 0) {
    int offset = (BAR_N - n) * (BAR_W + BAR_S);
    int bottom = GRAPH_Y + GRAPH_H - 1;
    for (int i = 0; i < n; i++) {
      int x = GRAPH_X + offset + i * (BAR_W + BAR_S);
      int h = (int)((values[i] - min) / range * (GRAPH_H - 1));
      if (h > 0) {
        al_rect(x, bottom - h, BAR_W, h + 1, 1, 0);
      }
    }
  }

  // draw info line
  int y = AL_H - 14;
  if (n > 0) {
    char num_min[16], num_max[16];
    snprintf(num_min, sizeof(num_min), fi->fmt, data_min);
    snprintf(num_max, sizeof(num_max), fi->fmt, data_max);
    snprintf(buf, sizeof(buf), "min %s  max %s", num_min, num_max);
    al_write(GRAPH_X, y, 0, 8, 1, buf, 0);
    char num[16];
    snprintf(num, sizeof(num), fi->fmt, values[n - 1]);
    snprintf(buf, sizeof(buf), "now %s %s @ %dm", num, fi->unit, span);
    al_write(AL_W - GRAPH_X, y, 0, 8, 1, buf, AL_WRITE_ALIGN_RIGHT);
  } else {
    snprintf(buf, sizeof(buf), "-- %s @ %dm", fi->unit, span);
    al_write(AL_W / 2, y, 0, 8, 1, buf, AL_WRITE_ALIGN_CENTER);
  }

  // draw title
  al_write(AL_W / 2, y, 0, 8, 1, fi->name, AL_WRITE_ALIGN_CENTER);
}

int main() {
  int field_idx = 0;
  int span_idx = 1;

  draw(field_idx, span_idx);

  for (;;) {
    al_yield_result_t ev = al_yield(0, 0);

    switch (ev) {
      case AL_YIELD_ESCAPE:
        return 0;
      case AL_YIELD_UP:
        field_idx = (field_idx + NUM_FIELDS - 1) % NUM_FIELDS;
        break;
      case AL_YIELD_DOWN:
        field_idx = (field_idx + 1) % NUM_FIELDS;
        break;
      case AL_YIELD_LEFT:
        if (span_idx > 0) span_idx--;
        break;
      case AL_YIELD_RIGHT:
        if (span_idx < NUM_SPANS - 1) span_idx++;
        break;
      default:
        continue;
    }

    draw(field_idx, span_idx);
  }
}
