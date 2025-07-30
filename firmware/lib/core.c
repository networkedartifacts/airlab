#include <naos.h>
#include <naos/auth.h>
#include <naos/sys.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/i2c.h>
#include <esp_sleep.h>

#include <al/core.h>
#include <al/power.h>

#include "internal.h"

#define AL_BUTTONS                                                                                               \
  (BIT64(AL_BUTTONS_A) | BIT64(AL_BUTTONS_B) | BIT64(AL_BUTTONS_C) | BIT64(AL_BUTTONS_D) | BIT64(AL_BUTTONS_E) | \
   BIT64(AL_BUTTONS_F))

static naos_mutex_t al_i2c_mutex;
static naos_auth_data_t al_auth_data = {0};

static al_trigger_t al_trigger() {
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
    } else if ((status & BIT64(AL_INT_IN)) != 0) {
      return AL_INTERRUPT;
    }
  }

  return AL_RESET;
}

static void al_int_task() {
  // check accelerometer
  al_accel_check();

  // check power
  al_power_check();
}

static void al_int_signal() {
  // defer check
  if (gpio_get_level(AL_INT_IN) == 0) {
    naos_defer_isr(al_int_task);
  }
}

al_trigger_t al_init() {
  // stop ULP program
  al_ulp_stop();

  // create mutex
  al_i2c_mutex = naos_mutex();

  // read authentication data
  bool auth = naos_auth_describe(&al_auth_data) == NAOS_AUTH_ERR_OK;

  // install interrupt service
  ESP_ERROR_CHECK(gpio_install_isr_service(0));

  // initialize SPI bus
  spi_bus_config_t spi = {
      .mosi_io_num = GPIO_NUM_38,
      .miso_io_num = -1,
      .sclk_io_num = GPIO_NUM_39,
      .max_transfer_sz = 5125,
      .flags = SPICOMMON_BUSFLAG_MASTER,
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi, SPI_DMA_CH_AUTO));

  // install I2C driver
  ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

  // reset I2C pins after ULP usage
  ESP_ERROR_CHECK(gpio_reset_pin(GPIO_NUM_1));
  ESP_ERROR_CHECK(gpio_reset_pin(GPIO_NUM_2));

  // configure I2C driver
  i2c_config_t i2c = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = GPIO_NUM_1,
      .scl_io_num = GPIO_NUM_2,
      .master.clk_speed = 100 * 1000,
  };
  ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c));

  // determine reset
  bool reset = !esp_sleep_get_wakeup_cause();

  // initialize modules
  al_power_init();
  al_buzzer_init();
  al_led_init(reset);
  al_accel_init(reset);
  al_buttons_init(reset);
  al_epd_init();
  al_clock_init();
  al_touch_init(reset);
  al_store_init();
  al_ulp_init(reset);
  al_sensor_init(reset);
  al_storage_init();

  // configure wakeup source
  uint64_t pin_mask = AL_BUTTONS | BIT64(AL_INT_IN);
  ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(pin_mask, ESP_EXT1_WAKEUP_ANY_LOW));

  // setup interrupt
  gpio_config_t cfg = {
      .pin_bit_mask = BIT64(AL_INT_IN),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));
  ESP_ERROR_CHECK(gpio_isr_handler_add(AL_INT_IN, al_int_signal, NULL));

  // get trigger
  al_trigger_t trigger = al_trigger();

  // log initialization
  naos_log("al_init: trigger=%d auth=%s rev=%d", trigger, auth ? "ok" : "failed", al_auth_data.revision);

  return trigger;
}

esp_err_t al_i2c_transfer(uint8_t addr, uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len, int timeout) {
  // acquire mutex
  naos_lock(al_i2c_mutex);

  // perform appropriate I2C transfer
  esp_err_t err;
  if (tx_len > 0 && rx_len > 0) {
    err = i2c_master_write_read_device(I2C_NUM_0, addr, tx, tx_len, rx, rx_len, pdMS_TO_TICKS(timeout));
  } else if (tx_len > 0) {
    err = i2c_master_write_to_device(I2C_NUM_0, addr, tx, tx_len, pdMS_TO_TICKS(timeout));
  } else {
    err = i2c_master_read_from_device(I2C_NUM_0, addr, rx, rx_len, pdMS_TO_TICKS(timeout));
  }

  // unlock mutex
  naos_unlock(al_i2c_mutex);

  return err;
}

al_trigger_t al_sleep(bool deep, uint64_t timeout) {
  // get power state
  al_power_state_t state = al_power_get();

  // sleep peripherals
  al_touch_sleep();

  // enable deep sleep hold
  gpio_deep_sleep_hold_en();

  // configure timeout
  if (timeout > 0) {
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(timeout * 1000));
  } else {
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(10 * 60 * 1000 * 1000));
  }

  // prepare deep sleep
  if (deep) {
    // enable low power mode if not USB
    al_sensor_low_power(!state.usb, false);

    // disable I2C access
    naos_lock(al_i2c_mutex);
    ESP_ERROR_CHECK(i2c_driver_delete(I2C_NUM_0));
    ESP_ERROR_CHECK(gpio_reset_pin(GPIO_NUM_2));
    ESP_ERROR_CHECK(gpio_reset_pin(GPIO_NUM_1));

    // start ULP program
    al_ulp_start();

    // enable ULP wake up
    ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
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

  return al_trigger();
}

void* al_alloc(size_t size) {
  // allocate memory from external RAM
  void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (ptr == NULL) {
    ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
  }
  return ptr;
}

void* al_calloc(size_t count, size_t size) {
  // allocate memory from external RAM
  void* ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM);
  if (ptr == NULL) {
    ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
  }
  return ptr;
}
