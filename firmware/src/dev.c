#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/i2c.h>

#define DEV_SELECT GPIO_NUM_25
#define DEV_LOAD GPIO_NUM_26

static spi_device_handle_t dev_device = NULL;

void dev_init() {
  // install interrupt service
  ESP_ERROR_CHECK(gpio_install_isr_service(0));

  // initialize bus
  spi_bus_config_t spi = {
      .mosi_io_num = 23,
      .miso_io_num = 19,
      .sclk_io_num = 18,
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
      .sda_io_num = 21,
      .scl_io_num = 22,
      .sda_pullup_en = true,
      .scl_pullup_en = true,
  };
  i2c.master.clk_speed = 100 * 1000;
  ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c));

  // initialize pin
  gpio_config_t pin = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = BIT64(DEV_LOAD),
  };
  ESP_ERROR_CHECK(gpio_config(&pin));

  // initialize device
  spi_device_interface_config_t dev = {
      .command_bits = 0,
      .address_bits = 0,
      .mode = 0b11,  // CPOL=1, CPHA=1
      .clock_speed_hz = SPI_MASTER_FREQ_20M,
      .spics_io_num = DEV_SELECT,
      .flags = SPI_DEVICE_HALFDUPLEX,  // negative CS
      .queue_size = 1,
  };
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &dev_device));

  // clear pin
  gpio_set_level(DEV_LOAD, 1);
}

uint8_t dev_shift() {
  // trigger load
  gpio_set_level(DEV_LOAD, 0);
  gpio_set_level(DEV_LOAD, 1);

  // run transaction
  spi_transaction_t tx = {
      .flags = SPI_TRANS_USE_RXDATA,
      .rxlength = 8,
  };
  ESP_ERROR_CHECK(spi_device_transmit(dev_device, &tx));

  return tx.rx_data[0];
}
