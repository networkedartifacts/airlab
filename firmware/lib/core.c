#include <naos.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/i2c.h>
#include <esp_sleep.h>

#include <al/core.h>

#include "internal.h"

#define AL_BUTTONS                                                                                               \
  (BIT64(AL_BUTTONS_A) | BIT64(AL_BUTTONS_B) | BIT64(AL_BUTTONS_C) | BIT64(AL_BUTTONS_D) | BIT64(AL_BUTTONS_E) | \
   BIT64(AL_BUTTONS_F))

void al_init() {
  // install interrupt service
  ESP_ERROR_CHECK(gpio_install_isr_service(0));

  // initialize SPI bus
  spi_bus_config_t spi = {
      .mosi_io_num = GPIO_NUM_38,
      .miso_io_num = -1,
      .sclk_io_num = GPIO_NUM_39,
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

  // initialize modules
  al_power_init();
  al_buzzer_init();
  al_led_init();
  al_accel_init();
  al_buttons_init();
  al_epd_init();
  al_touch_init();
  al_sensor_init();

  // sync clock
  al_clock_init();

  // configure wakeup source
  uint64_t pin_mask = AL_BUTTONS | BIT64(AL_ACCEL_INT);
  ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(pin_mask, ESP_EXT1_WAKEUP_ANY_LOW));
}

void al_sleep(bool deep, uint64_t timeout) {
  // sleep peripherals
  al_touch_sleep();

  // enable deep sleep hold
  gpio_deep_sleep_hold_en();

  // configure timeout
  if (timeout > 0) {
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(timeout * 1000));
  } else {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  }

  // perform sleep
  if (deep) {
    esp_deep_sleep_start();
  } else {
    ESP_ERROR_CHECK(esp_light_sleep_start());
  }

  // disable deep sleep hold
  gpio_deep_sleep_hold_dis();

  // wake peripherals
  al_touch_wake();
}

al_trigger_t al_trigger() {
  // get cause
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  // handle timer
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    return AL_TIMEOUT;
  }

  // handle external
  if (cause == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t status = esp_sleep_get_ext1_wakeup_status();
    if ((status & AL_BUTTONS) != 0) {
      return AL_BUTTON;
    } else if ((status & BIT64(AL_ACCEL_INT)) != 0) {
      return AL_MOTION;
    }
  }

  return AL_RESET;
}
