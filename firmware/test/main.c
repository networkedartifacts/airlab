#include <unity.h>

void suite_config();
void suite_font();
void suite_scr();
void suite_bubbles();
void suite_sample();
void suite_store();
void suite_dat();

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  suite_config();
  suite_font();
  suite_scr();
  suite_bubbles();
  suite_sample();
  suite_store();
  suite_dat();
  return UNITY_END();
}
