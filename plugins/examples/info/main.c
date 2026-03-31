#include "../../al.h"

int main() {
  int n = 0;

  for (;;) {
    // clear screen
    al_clear(0);

    // handle screen
    char buf[128];
    switch (n) {
      case 0: {
        float battery_level = al_info(AL_INFO_BATTERY_LEVEL);
        float battery_voltage = al_info(AL_INFO_BATTERY_VOLTAGE);
        float power_usb = al_info(AL_INFO_POWER_USB);
        float power_charging = al_info(AL_INFO_POWER_CHARGING);
        float connected = al_info(AL_INFO_CONNECTED);
        snprintf(buf, sizeof(buf), "Battery: %.2f (%.2f)\nUSB Power: %.0f\nCharging: %.0f\nConnected: %.0f",
                 battery_level, battery_voltage, power_usb, power_charging, connected);
        break;
      }
      case 1: {
        float temperature = al_info(AL_INFO_SENSOR_TEMPERATURE);
        float humidity = al_info(AL_INFO_SENSOR_HUMIDITY);
        float co2 = al_info(AL_INFO_SENSOR_CO2);
        float voc = al_info(AL_INFO_SENSOR_VOC);
        float nox = al_info(AL_INFO_SENSOR_NOX);
        float pressure = al_info(AL_INFO_SENSOR_PRESSURE);
        float fahrenheit = al_info(AL_INFO_FAHRENHEIT);
        snprintf(buf, sizeof(buf),
                 "Temp: %.1f (F: %.0f)\nHumidity: %.1f\nCO2: %.0f\nVOC: %.0f\nNOx: %.0f\nPressure: %.0f", temperature,
                 fahrenheit, humidity, co2, voc, nox, pressure);
        break;
      }
      case 2: {
        int64_t store_start = al_store_info(AL_STORE_INFO_START);
        int64_t store_count_all = al_store_info(AL_STORE_INFO_COUNT_ALL);
        int64_t store_count_short = al_store_info(AL_STORE_INFO_COUNT_SHORT);
        int64_t store_count_long = al_store_info(AL_STORE_INFO_COUNT_LONG);
        snprintf(buf, sizeof(buf), "Start: %lld\nAll: %lld\nShort: %lld\nLong: %lld", store_start, store_count_all,
                 store_count_short, store_count_long);
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
