#include <naos.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/i2c.h>

#include "sig.h"
#include "pwr.h"
#include "btn.h"
#include "epd.h"
#include "gfx.h"
#include "sns.h"
#include "dat.h"
#include "rec.h"
#include "scr.h"

static void dev_init() {
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
}

static void setup() {
  // initialize
  dev_init();
  sig_init();
  pwr_init();
  btn_init();
  epd_init();
  gfx_init();
  sns_init();
  dat_init();
  rec_init();

  // run screen
  scr_run();
}

static naos_config_t config = {
    .device_type = "airlab",
    .device_version = "0.1.0",
    .setup_callback = setup,
};

void app_main() {
  // run naos
  naos_init(&config);
  naos_start();
}
