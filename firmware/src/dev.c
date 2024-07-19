#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/i2c.h>

void dev_init() {
  // install interrupt service
  ESP_ERROR_CHECK(gpio_install_isr_service(0));

  // initialize bus
  spi_bus_config_t spi = {
      .mosi_io_num = GPIO_NUM_41,
      .miso_io_num = -1,
      .sclk_io_num = GPIO_NUM_42,
      .quadhd_io_num = -1,
      .quadwp_io_num = -1,
      .max_transfer_sz = 8192,
      .flags = SPICOMMON_BUSFLAG_MASTER,
      .intr_flags = 0,
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi, SPI_DMA_CH_AUTO));

  // install driver
  ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

  // configure I2C driver
  i2c_config_t i2c = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = GPIO_NUM_2,
      .scl_io_num = GPIO_NUM_1,
  };
  i2c.master.clk_speed = 100 * 1000;
  ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c));
}
