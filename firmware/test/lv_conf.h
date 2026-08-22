#ifndef LV_CONF_H
#define LV_CONF_H

// Minimal LVGL configuration for the host-side tests. The text layout options
// must match the device configuration in naos/tree/sdkconfig (CONFIG_LV_TXT_*).

#define LV_COLOR_DEPTH 1
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN 0

// use the standard library for memory and formatting
#define LV_MEM_CUSTOM 1
#define LV_SPRINTF_CUSTOM 1
#define LV_USE_LOG 0

#endif  // LV_CONF_H
