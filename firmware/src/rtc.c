#include <naos.h>
#include <driver/i2c.h>
#include <esp_log.h>

#define RTC_ADDR 0x68

static void rtc_read(uint8_t reg, uint8_t *buf, size_t read) {
  // write and read device
  ESP_ERROR_CHECK(i2c_master_write_read_device(I2C_NUM_0, RTC_ADDR, &reg, 1, buf, read, 1000));
}

void rtc2_init() {
  // read RTC fully
  uint8_t buf[10] = {0};
  rtc_read(0, buf, 10);
  ESP_LOG_BUFFER_HEX("RTC", rtc_read, 10);
  // 36 81 00 ed 03 fd 04 22 41 10
}
