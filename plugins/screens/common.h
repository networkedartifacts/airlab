#pragma once

#include "../al.h"

/* === Temperature Helpers === */

static bool al_fahrenheit() {
  // get info
  return al_info(AL_INFO_FAHRENHEIT) > 0;
}

static float al_temp_convert(float celsius) {
  // handle fahrenheit
  if (al_fahrenheit()) {
    return celsius * 9.0f / 5.0f + 32.0f;
  }

  return celsius;
}

static const char *al_temp_unit() {
  // return unit
  return al_fahrenheit() ? "°F" : "°C";
}

static float al_temp_min() {
  // return min
  return al_fahrenheit() ? 32.0f : 0.0f;
}

static float al_temp_max() {
  // return max
  return al_fahrenheit() ? 122.0f : 50.0f;
}

static float al_sensor_value(al_info_t info) {
  // get value
  float val = al_info(info);

  // convert temperature
  if (info == AL_INFO_SENSOR_TEMPERATURE) {
    val = al_temp_convert(val);
  }

  return val;
}

/* === Sensor Definitions === */

#define AL_SENSOR_CO2 0
#define AL_SENSOR_TMP 1
#define AL_SENSOR_HUM 2
#define AL_SENSOR_VOC 3
#define AL_SENSOR_NOX 4
#define AL_SENSOR_PRS 5
#define AL_SENSOR_COUNT 6

typedef struct {
  al_info_t info;
  al_store_query_field_t field;
  const char *unit;
  const char *fmt;
  float min_val;
  float max_val;
} al_sensor_t;

static al_sensor_t al_sensors[AL_SENSOR_COUNT] = {
    {AL_INFO_SENSOR_CO2, AL_STORE_QUERY_CO2, "ppm", "%.0f", 400.0f, 2000.0f},
    {AL_INFO_SENSOR_TEMPERATURE, AL_STORE_QUERY_TMP, "°C", "%.1f", 0.0f, 50.0f},
    {AL_INFO_SENSOR_HUMIDITY, AL_STORE_QUERY_HUM, "% RH", "%.1f", 0.0f, 100.0f},
    {AL_INFO_SENSOR_VOC, AL_STORE_QUERY_VOC, "VOC", "%.0f", 0.0f, 500.0f},
    {AL_INFO_SENSOR_NOX, AL_STORE_QUERY_NOX, "NOx", "%.0f", 0.0f, 200.0f},
    {AL_INFO_SENSOR_PRESSURE, AL_STORE_QUERY_PRS, "hPa", "%.0f", 900.0f, 1100.0f},
};

static void al_patch_temp(al_sensor_t *s) {
  s->unit = al_temp_unit();
  s->min_val = al_temp_min();
  s->max_val = al_temp_max();
}

static int al_find_sensor(const char *key, const char *def) {
  char value[32] = {0};
  al_config_get_s(key, value, sizeof(value));
  const char *v = strlen(value) > 0 ? value : def;
  if (strlen(v) == 0) return -1;
  if (strcmp(v, "co2") == 0) return AL_SENSOR_CO2;
  if (strcmp(v, "tmp") == 0) return AL_SENSOR_TMP;
  if (strcmp(v, "hum") == 0) return AL_SENSOR_HUM;
  if (strcmp(v, "voc") == 0) return AL_SENSOR_VOC;
  if (strcmp(v, "nox") == 0) return AL_SENSOR_NOX;
  if (strcmp(v, "prs") == 0) return AL_SENSOR_PRS;
  return -1;
}

static float al_normalize(float val, float min_val, float max_val) {
  float denom = max_val - min_val;
  float ratio = denom > 0.0f ? (val - min_val) / denom : 0.0f;
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  return ratio;
}
