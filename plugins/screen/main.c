#include "../al.h"

int main() {
  // collect info
  float tmp = al_info(AL_INFO_SENSOR_TEMPERATURE);
  float hum = al_info(AL_INFO_SENSOR_HUMIDITY);
  float co2 = al_info(AL_INFO_SENSOR_CO2);

  // format info
  char tmp_str[32];
  char hum_str[32];
  char co2_str[32];
  snprintf(tmp_str, sizeof(tmp_str), "%.1f °C", tmp);
  snprintf(hum_str, sizeof(hum_str), "%.1f %%", hum);
  snprintf(co2_str, sizeof(co2_str), "%.0f ppm", co2);

  // write text
  al_write(20, 20, 0, 16, 1, tmp_str, 0);
  al_write(AL_W - 20, 20, 0, 16, 1, hum_str, AL_WRITE_ALIGN_RIGHT);
  al_write(AL_W / 2, AL_H / 2 - 12, 0, 24, 1, co2_str, AL_WRITE_ALIGN_CENTER);

  return 0;
}
