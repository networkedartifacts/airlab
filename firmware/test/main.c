#include <unity.h>

void suite_config();
void suite_font();
void suite_scr();
void suite_pwr();
void suite_bubbles();
void suite_sample();
void suite_store();
void suite_clock();
void suite_dat();
void suite_rec();
void suite_sensor_hal();
void suite_sensor();

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  suite_config();
  suite_font();
  suite_scr();
  suite_pwr();
  suite_bubbles();
  suite_sample();
  suite_store();
  suite_clock();
  suite_dat();
  suite_rec();
  suite_sensor_hal();
  suite_sensor();
  return UNITY_END();
}
