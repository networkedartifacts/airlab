#pragma once

#include "../al.h"

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
