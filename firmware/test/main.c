#include <unity.h>

void suite_config();
void suite_bubbles();

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  suite_config();
  suite_bubbles();
  return UNITY_END();
}
