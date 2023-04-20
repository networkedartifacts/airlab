#include <naos.h>
#include <naos_sys.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_sleep.h>

#include "dev.h"
#include "sig.h"

#define BTN_DEBUG false
#define BTN_SELECT GPIO_NUM_25
#define BTN_LOAD GPIO_NUM_26
#define BTN_WAKE (DEV_V == 0 ? GPIO_NUM_36 : GPIO_NUM_33)

static uint8_t btn_state = 0x00;
static spi_device_handle_t btn_device = NULL;

static sig_event_t btn_events[] = {
    SIG_ESCAPE, SIG_LEFT, SIG_RIGHT, SIG_DOWN, SIG_UP, SIG_ENTER,
};

static void btn_check() {
  // trigger load
  gpio_set_level(BTN_LOAD, 0);
  gpio_set_level(BTN_LOAD, 1);

  // run transaction
  spi_transaction_t tx = {
      .flags = SPI_TRANS_USE_RXDATA,
      .rxlength = 8,
  };
  ESP_ERROR_CHECK(spi_device_transmit(btn_device, &tx));

  // get inverted state (1 = pressed)
  uint8_t state = tx.rx_data[0] ^ 0x3f;

  // get changed buttons
  uint8_t changed = state ^ btn_state;

  // dispatch button changes
  for (int8_t i = 0; i < 6; i++) {
    if (changed & (1 << i)) {
      if ((state & (1 << i)) != 0) {
        sig_dispatch(btn_events[i]);
      }
      if (BTN_DEBUG) {
        naos_log("btn: changed %d=%d", i, (btn_state & (1 << i)) != 0);
      }
    }
  }

  // update state
  btn_state = state;
}

void btn_init() {
  // initialize pin
  gpio_config_t pin = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = BIT64(BTN_LOAD),
  };
  ESP_ERROR_CHECK(gpio_config(&pin));

  // initialize device
  spi_device_interface_config_t dev = {
      .command_bits = 0,
      .address_bits = 0,
      .mode = 0b11,  // CPOL=1, CPHA=1
      .clock_speed_hz = SPI_MASTER_FREQ_20M,
      .spics_io_num = BTN_SELECT,
      .flags = SPI_DEVICE_HALFDUPLEX,  // negative CS
      .queue_size = 1,
  };
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &btn_device));

  // clear pin
  gpio_set_level(BTN_LOAD, 1);

  // configure gpio
  pin.mode = GPIO_MODE_INPUT;
  pin.pin_bit_mask = BIT64(BTN_WAKE);
  ESP_ERROR_CHECK(gpio_config(&pin));

  // configure wakeup source
  ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(BTN_WAKE, 0));

  // start timer
  naos_repeat("btn", 25, btn_check);  // 50 Hz
}
