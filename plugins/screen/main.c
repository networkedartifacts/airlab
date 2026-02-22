#include "../al.h"

int main() {
  // get config
  char primary[32] = {0};
  al_config_get_s("primary", primary, sizeof(primary));

  // primary value
  char pri_str[32];
  if (strcmp(primary, "co2") == 0) {
    float co2 = al_info(AL_INFO_SENSOR_CO2);
    snprintf(pri_str, sizeof(pri_str), "%.0f ppm", co2);
  } else if (strcmp(primary, "voc") == 0) {
    float voc = al_info(AL_INFO_SENSOR_VOC);
    snprintf(pri_str, sizeof(pri_str), "%.0f VOC", voc);
  } else if (strcmp(primary, "nox") == 0) {
    float nox = al_info(AL_INFO_SENSOR_NOX);
    snprintf(pri_str, sizeof(pri_str), "%.0f NOx", nox);
  } else {
    snprintf(pri_str, sizeof(pri_str), "No primary");
  }

  // secondary values
  float tmp = al_info(AL_INFO_SENSOR_TEMPERATURE);
  float hum = al_info(AL_INFO_SENSOR_HUMIDITY);
  char tmp_str[32];
  char hum_str[32];
  snprintf(tmp_str, sizeof(tmp_str), "%.1f °C", tmp);
  snprintf(hum_str, sizeof(hum_str), "%.1f %%", hum);

  // write text
  al_write(20, 20, 0, 16, 1, tmp_str, 0);
  al_write(AL_W - 20, 20, 0, 16, 1, hum_str, AL_WRITE_ALIGN_RIGHT);
  al_write(AL_W / 2, AL_H / 2 - 12, 0, 24, 1, pri_str, AL_WRITE_ALIGN_CENTER);

  return 0;
}
