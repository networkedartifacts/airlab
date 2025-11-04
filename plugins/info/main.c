#include <stdio.h>

#include "../al.h"

int main() {
  int n = 0;

  for (;;) {
    // clear screen
    al_clear(0);

    // log number
    al_logf("Screen: %d", n);

    // handle screen
    char buf[128];
    switch (n) {
      case 0: {
        float battery_level = al_info(AL_INFO_BATTERY_LEVEL);
        float battery_voltage = al_info(AL_INFO_BATTERY_VOLTAGE);
        float power_usb = al_info(AL_INFO_POWER_USB);
        float powr_chaging = al_info(AL_INGO_POWER_CHARGING);
        snprintf(buf, sizeof(buf), "Battery: %.2f (%.2f)\nUSB Power: %.0f\nCharging: %.0f", battery_level,
                 battery_voltage, power_usb, powr_chaging);
        break;
      }
      case 1: {
        float temperature = al_info(AL_INFO_SENSOR_TEMPERATURE);
        float humidity = al_info(AL_INFO_SENSOR_HUMIDITY);
        float co2 = al_info(AL_INFO_SENSOR_CO2);
        float voc = al_info(AL_INFO_SENSOR_VOC);
        float nox = al_info(AL_INFO_SENSOR_NOX);
        float pressure = al_info(AL_INFO_SENSOR_PRESSURE);
        snprintf(buf, sizeof(buf), "Temp: %.1f\nHumidity: %.1f\nCO2: %.0f\nVOC: %.0f\nNOx: %.0f\nPressure: %.0f",
                 temperature, humidity, co2, voc, nox, pressure);
        break;
      }
      case 2: {
        float store_short = al_info(AL_INGO_STORE_SHORT);
        float store_long = al_info(AL_INGO_STORE_LONG);
        snprintf(buf, sizeof(buf), "Short Store: %.0f\nLong Store: %.0f", store_short, store_long);
        break;
      }
      case 3: {
        float accel_front = al_info(AL_INFO_ACCEL_FRONT);
        float accel_rotation = al_info(AL_INFO_ACCEL_ROTATION);
        snprintf(buf, sizeof(buf), "Accel Front: %.0f\nAccel Rotation: %.0f", accel_front, accel_rotation);
        break;
      }
      case 4: {
        float storage_int = al_info(AL_INFO_STORAGE_INT);
        float storage_ext = al_info(AL_INFO_STORAGE_EXT);
        snprintf(buf, sizeof(buf), "Internal Storage: %.2f\nExternal Storage: %.2f", storage_int, storage_ext);
        break;
      }
    }

    // write to screen
    al_write(8, 8, 2, 16, 1, buf, 0);

    // handle exit
    if (al_yield(0, 0) == AL_YIELD_ESCAPE) {
      return 0;
    }

    // next screen
    n = (n + 1) % 5;
  }
}
