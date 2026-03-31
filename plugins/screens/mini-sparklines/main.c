#include "../common.h"

#define NUM_SENSORS 6
#define SPARK_W 240
#define SPARK_PAD 3
#define SPARK_X 4
#define ROW_H (AL_H / NUM_SENSORS)
#define SPAN_MS (5 * 60 * 1000)  // 5 minutes

typedef struct {
  al_info_t info;
  al_store_query_field_t field;
  const char *unit;
  const char *fmt;
} sensor_t;

static sensor_t sensors[NUM_SENSORS] = {
    {AL_INFO_SENSOR_CO2, AL_STORE_QUERY_CO2, "ppm", "%.0f"},
    {AL_INFO_SENSOR_TEMPERATURE, AL_STORE_QUERY_TMP, "°C", "%.1f"},
    {AL_INFO_SENSOR_HUMIDITY, AL_STORE_QUERY_HUM, "% RH", "%.1f"},
    {AL_INFO_SENSOR_VOC, AL_STORE_QUERY_VOC, "VOC", "%.0f"},
    {AL_INFO_SENSOR_NOX, AL_STORE_QUERY_NOX, "NOx", "%.0f"},
    {AL_INFO_SENSOR_PRESSURE, AL_STORE_QUERY_PRS, "hPa", "%.0f"},
};

int main() {
  // determine time range (shared across all sensors)
  int store_stop = (int)al_store_info(AL_STORE_INFO_LENGTH);
  int resolution = SPAN_MS / SPARK_W;
  int first = store_stop - SPAN_MS;
  int count = SPARK_W;
  if (first < 0) {
    first = 0;
    count = store_stop / resolution;
    if (count < 1 && store_stop > 0) count = 1;
    if (count > SPARK_W) count = SPARK_W;
  }

  // patch temperature
  sensors[1].unit = al_temp_unit();

  // clear screen
  al_clear(0);

  // prepare values
  float values[SPARK_W] = {0};

  // draw each sensor row
  for (int si = 0; si < NUM_SENSORS; si++) {
    // get sensor
    sensor_t *s = &sensors[si];

    // compute row layout
    int row_y = si * ROW_H;
    int spark_top = row_y + SPARK_PAD;
    int spark_bot = row_y + ROW_H - SPARK_PAD;
    int spark_h = spark_bot - spark_top;

    // query history for this sensor
    for (int j = 0; j < SPARK_W; j++) values[j] = 0;
    int n = (resolution > 0 && count > 0) ? al_store_query(s->field, values, count, first, resolution) : 0;

    // convert stored temperature values
    if (s->info == AL_INFO_SENSOR_TEMPERATURE) {
      for (int j = 0; j < n; j++) {
        values[j] = al_temp_convert(values[j]);
      }
    }

    // min/max for dynamic normalization
    float s_min = 0, s_max = 0;
    if (n > 0) {
      s_min = values[0];
      s_max = values[0];
      for (int j = 1; j < n; j++) {
        if (values[j] < s_min) s_min = values[j];
        if (values[j] > s_max) s_max = values[j];
      }
    }
    float mid = (s_min + s_max) / 2.0f;
    if (s_max - s_min < 10.0f) {
      s_min = mid - 5.0f;
      s_max = mid + 5.0f;
    }
    float range = s_max - s_min;

    // draw sparkline, right-aligned
    int offset = SPARK_W - n;
    for (int j = 0; j < n; j++) {
      float norm = (values[j] - s_min) / range;
      if (norm < 0.0f) norm = 0.0f;
      if (norm > 1.0f) norm = 1.0f;
      int x = SPARK_X + offset + j;
      int y = spark_bot - (int)(norm * spark_h + 0.5f);
      al_rect(x - 1, y - 1, 3, 3, 1, 0);
    }

    // current value + unit, right-aligned
    float val = al_sensor_value(s->info);
    char val_str[16], buf[32];
    snprintf(val_str, sizeof(val_str), s->fmt, val);
    snprintf(buf, sizeof(buf), "%s %s", val_str, s->unit);
    al_write(AL_W - 46, row_y + (ROW_H - 8) / 2, 0, 8, 1, buf, 0);
  }

  return 0;
}
