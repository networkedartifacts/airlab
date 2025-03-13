#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/i2c.h>
#include <esp_sleep.h>

#include "dev.h"

#define DEV_MOSI GPIO_NUM_38
#define DEV_SCLK GPIO_NUM_39

void dev_init() {
  // install interrupt service
  ESP_ERROR_CHECK(gpio_install_isr_service(0));

  // initialize SPI bus
  spi_bus_config_t spi = {
      .mosi_io_num = DEV_MOSI,
      .miso_io_num = -1,
      .sclk_io_num = DEV_SCLK,
      .max_transfer_sz = 8192,
      .flags = SPICOMMON_BUSFLAG_MASTER,
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi, SPI_DMA_CH_AUTO));

  // install I2C driver
  ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

  // configure I2C driver
  i2c_config_t i2c = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = GPIO_NUM_1,
      .scl_io_num = GPIO_NUM_2,
      .master.clk_speed = 100 * 1000,
  };
  ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c));

  // configure wakeup source
  uint64_t pin_mask = BIT64(DEV_BTN_A) | BIT64(DEV_BTN_B) | BIT64(DEV_BTN_C) | BIT64(DEV_BTN_D) | BIT64(DEV_BTN_E) |
                      BIT64(DEV_BTN_F) | BIT64(DEV_INT_ACC);
  ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(pin_mask, ESP_EXT1_WAKEUP_ANY_LOW));
}
