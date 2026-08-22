#include <unity.h>
#include <lvgl.h>

// device configuration extracted from naos/tree/sdkconfig by the Makefile
#include "device_conf.h"

static void test_config_matches_device() {
  // verify text layout options in lv_conf.h match the device configuration
#ifdef DEVICE_LV_TXT_ENC_UTF8
  TEST_ASSERT_EQUAL_INT(LV_TXT_ENC_UTF8, LV_TXT_ENC);
#else
  TEST_ASSERT_EQUAL_INT(LV_TXT_ENC_ASCII, LV_TXT_ENC);
#endif
  TEST_ASSERT_EQUAL_STRING(DEVICE_LV_TXT_BREAK_CHARS, LV_TXT_BREAK_CHARS);
  TEST_ASSERT_EQUAL_INT(DEVICE_LV_TXT_LINE_BREAK_LONG_LEN, LV_TXT_LINE_BREAK_LONG_LEN);
  TEST_ASSERT_EQUAL_STRING(DEVICE_LV_TXT_COLOR_CMD, LV_TXT_COLOR_CMD);
  TEST_ASSERT_EQUAL_INT(DEVICE_LV_COLOR_DEPTH, LV_COLOR_DEPTH);
}

void suite_config() {
  RUN_TEST(test_config_matches_device);
}
