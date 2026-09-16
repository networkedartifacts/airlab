#include <unity.h>

void suite_config();
void suite_font();
void suite_scr();
void suite_chk();
void suite_chk_vent();
void suite_chk_measure();
void suite_chk_stove();
void suite_qrcodegen();
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
  suite_chk();
  suite_chk_vent();
  suite_chk_measure();
  suite_chk_stove();
  suite_qrcodegen();
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
