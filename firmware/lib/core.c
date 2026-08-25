#include <naos.h>
#include <naos/auth.h>
#include <naos/sys.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/i2c.h>
#include <esp_rom_sys.h>
#include <esp_sleep.h>
#include <esp_pm.h>

#include <al/core.h>
#include <al/clock.h>
#include <al/sensor.h>

#include "internal.h"

#define AL_BUTTONS                                                                                               \
  (BIT64(AL_BUTTONS_A) | BIT64(AL_BUTTONS_B) | BIT64(AL_BUTTONS_C) | BIT64(AL_BUTTONS_D) | BIT64(AL_BUTTONS_E) | \
   BIT64(AL_BUTTONS_F))

#define AL_REBOOT_MAGIC 0x52454254  // "REBT"

// total attempts and delay between them for retried I2C transfers
#define AL_I2C_ATTEMPTS 4
#define AL_I2C_RETRY_DELAY 10

static naos_mutex_t al_i2c_mutex;
static naos_auth_data_t al_auth_data = {0};
static esp_sleep_wakeup_cause_t al_wakeup_cause_val;
static uint64_t al_wakeup_status_val;

static void al_wakeup_capture() {
  // latch cause and status
  al_wakeup_cause_val = esp_sleep_get_wakeup_cause();
  al_wakeup_status_val = esp_sleep_get_ext1_wakeup_status();
}

esp_sleep_wakeup_cause_t al_wakeup_cause() { return al_wakeup_cause_val; }

uint64_t al_wakeup_status() { return al_wakeup_status_val; }

static al_trigger_t al_trigger() {
  // get latched cause
  esp_sleep_wakeup_cause_t cause = al_wakeup_cause();

  // handle timer
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    return AL_TIMEOUT;
  }

  // handle external
  if (cause == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t status = al_wakeup_status();
    if ((status & AL_BUTTONS) != 0) {
      return AL_BUTTON;
    } else if ((status & BIT64(AL_INT_IN)) != 0) {
      return AL_INTERRUPT;
    }
  }

  // the interrupt line is additionally armed as a level-triggered GPIO wakeup
  // to catch events missed by the edge interrupt, treat those wakes the same
  if (cause == ESP_SLEEP_WAKEUP_GPIO) {
    return AL_INTERRUPT;
  }

  // log unmapped causes that fall through to a reset
  if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
    naos_log("al: unmapped wakeup cause=%d", cause);
  }

  return AL_RESET;
}

static void al_int_task() {
  // check accelerometer
  al_accel_check();

  // check power
  al_power_check();

  // re-arm the interrupt now that the sources have been cleared; if the line
  // is still or again low, a new interrupt fires and loops through here again
  // at a bounded, scheduler-paced rate
  ESP_ERROR_CHECK(gpio_intr_enable(AL_INT_IN));
}

static void al_int_signal() {
  // quell the interrupt until serviced: gpio_wakeup_enable() rewires the pin
  // from edge to low-level triggering, and the line is latched low until the
  // sources are cleared over I2C; left enabled, the interrupt re-fires
  // continuously and starves the deferred task (and the I2C interrupt) on the
  // same core that would clear it, tripping the interrupt watchdog; therefore,
  // the interrupt is strictly re-armed from the task only
  gpio_intr_disable(AL_INT_IN);

  // defer check; on the rare failure, the interrupt stays disabled and the
  // periodic checks will still clear the sources
  naos_defer_isr("al-int", al_int_task);
}

static void al_i2c_clear_bus() {
  // NOTE: once migrated to the new I2C driver (driver/i2c_master.h), this
  // routine can be replaced by the public i2c_master_bus_reset(); the legacy
  // driver only clears the bus internally after failed transfers

  // drive SCL open-drain high and sample SDA through the external pull-ups
  gpio_config_t scl = {
      .pin_bit_mask = BIT64(GPIO_NUM_2),
      .mode = GPIO_MODE_OUTPUT_OD,
  };
  ESP_ERROR_CHECK(gpio_config(&scl));
  ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, 1));
  gpio_config_t sda = {
      .pin_bit_mask = BIT64(GPIO_NUM_1),
      .mode = GPIO_MODE_INPUT_OUTPUT_OD,
  };
  ESP_ERROR_CHECK(gpio_config(&sda));
  ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_1, 1));

  // skip if the bus is idle
  if (gpio_get_level(GPIO_NUM_1)) {
    return;
  }

  // a slave halted mid-transaction holds SDA low until it is clocked through
  // its remaining bits and sees a STOP: clock out up to nine pending bits
  // (I2C-bus specification 3.1.16) at the 100 kHz bus speed
  naos_log("al-i2c: SDA held low, clearing bus");
  for (int i = 0; i < 9 && !gpio_get_level(GPIO_NUM_1); i++) {
    gpio_set_level(GPIO_NUM_2, 0);
    esp_rom_delay_us(5);
    gpio_set_level(GPIO_NUM_2, 1);
    esp_rom_delay_us(5);
  }

  // generate a STOP condition (SDA low to high while SCL is high)
  gpio_set_level(GPIO_NUM_1, 0);
  esp_rom_delay_us(5);
  gpio_set_level(GPIO_NUM_1, 1);
  esp_rom_delay_us(5);
}

al_trigger_t al_init() {
  // latch the wakeup cause and status before automatic light sleeps can
  // overwrite them with their own timer wakeups
  al_wakeup_capture();

  // stop ULP program
  al_ulp_stop();

  // create mutex
  al_i2c_mutex = naos_mutex();

  // read authentication data
  bool auth = naos_auth_describe(&al_auth_data) == NAOS_AUTH_ERR_OK;

  // disarm all pin interrupts before installing the service, which enables the
  // shared GPIO interrupt while no pin handler exists yet: a panic reset only
  // resets the CPUs and a few peripherals, but not the GPIO peripheral, so the
  // enables and trigger types of the previous run survive; a pin left on the
  // low-level trigger with its line still latched low then re-fires as soon as
  // the interrupt goes live and starves this core for good, as the task that
  // would clear the sources over I2C runs on it too
  for (int pin = 0; pin < GPIO_NUM_MAX; pin++) {
    if (GPIO_IS_VALID_GPIO(pin)) {
      ESP_ERROR_CHECK(gpio_intr_disable(pin));
    }
  }

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

  // clear a stuck bus before the driver takes over the pins
  al_i2c_clear_bus();

  // configure I2C driver
  i2c_config_t i2c = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = GPIO_NUM_1,
      .scl_io_num = GPIO_NUM_2,
      .master.clk_speed = 100 * 1000,
  };
  ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c));

  // determine reset
  bool reset = !al_wakeup_cause();

  // initialize modules
  al_power_init();
  al_buzzer_init();
  al_led_init(reset);
  al_accel_init(reset);
  al_buttons_init();
  al_epd_init();
  al_clock_init(reset);
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

  // edge interrupts are missed during light sleep, therefore also wake on the
  // level-latched interrupt line to not lose accelerometer and power events
  ESP_ERROR_CHECK(gpio_wakeup_enable(AL_INT_IN, GPIO_INTR_LOW_LEVEL));
  ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

  // hold GPIO during sleep
  ESP_ERROR_CHECK(gpio_hold_en(AL_INT_IN));

  // initialize power management with a pinned CPU frequency, as frequency
  // switches race the crypto engines whose clocks scale with the CPU clock
  // (observed SHA hang during WiFi roams; only the MPI driver guards itself
  // with locks) and also the MSPI timing retune when switching down to the
  // XTAL frequency (IDF v5.4.3); automatic light sleep provides the actual
  // savings and already covers the busy-waited display refreshes
  esp_pm_config_t pm = {
      .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
      .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
      .light_sleep_enable = true,
  };
  ESP_ERROR_CHECK(esp_pm_configure(&pm));

  // get trigger
  al_trigger_t trigger = al_trigger();

  // log initialization
  naos_log("al-init: trigger=%d auth=%s rev=%d", trigger, auth ? "ok" : "failed", al_auth_data.revision);

  return trigger;
}

esp_err_t al_i2c_transfer(uint8_t addr, uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len, int timeout,
                          bool retry) {
  // acquire mutex
  naos_lock(al_i2c_mutex);

  // perform appropriate I2C transfer, retrying transient failures if requested:
  // devices sometimes do not acknowledge a transfer at all, and the driver only
  // resets its state machine after a few consecutive acknowledgement errors; the
  // mutex is held across the attempts to not interleave a retried operation
  esp_err_t err;
  for (int attempt = 0;; attempt++) {
    if (tx_len > 0 && rx_len > 0) {
      err = i2c_master_write_read_device(I2C_NUM_0, addr, tx, tx_len, rx, rx_len, pdMS_TO_TICKS(timeout));
    } else if (tx_len > 0) {
      err = i2c_master_write_to_device(I2C_NUM_0, addr, tx, tx_len, pdMS_TO_TICKS(timeout));
    } else {
      err = i2c_master_read_from_device(I2C_NUM_0, addr, rx, rx_len, pdMS_TO_TICKS(timeout));
    }
    if (err == ESP_OK || !retry || attempt >= AL_I2C_ATTEMPTS - 1) {
      break;
    }
    naos_log("al-i2c: retry addr=0x%02X err=%d", addr, err);
    naos_delay(AL_I2C_RETRY_DELAY);
  }

  // unlock mutex
  naos_unlock(al_i2c_mutex);

  return err;
}

void al_sleep(bool ulp, uint64_t timeout) {
  // stop PM measurements first, as suspending the PM sensor also prevents the
  // sensor monitor from starting a new measurement while we prepare to sleep,
  // which would then have to be stopped again below
  al_sensor_pm_sleep();

  // prepare the sensor for the ULP handover, or turn it off entirely and sync
  // the ULP memory if the ULP stays disabled
  if (ulp) {
    al_sensor_sleep();
  } else {
    al_sensor_off();
    al_ulp_sync();
  }

  // disable the charger watchdog, as an expiry during sleep would reset the
  // charger and wake the device via the interrupt line
  al_power_sleep();

  // sleep peripherals after all other I2C traffic
  al_touch_sleep();

  // mirror a pending clock step to the RTC, as the periodic sync may not have
  // run since the step was applied and the boot seed would revert it
  al_clock_flush();

  // enable deep sleep hold
  gpio_deep_sleep_hold_en();

  // configure timeout
  if (timeout > 0) {
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(timeout * 1000));
  } else {
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(10 * 60 * 1000 * 1000));
  }

  // disable I2C access
  naos_lock(al_i2c_mutex);
  ESP_ERROR_CHECK(i2c_driver_delete(I2C_NUM_0));
  ESP_ERROR_CHECK(gpio_reset_pin(GPIO_NUM_2));
  ESP_ERROR_CHECK(gpio_reset_pin(GPIO_NUM_1));

  // start ULP program and enable its wake up, unless a floor sleep is
  // requested that leaves the RTC domain unpowered
  if (ulp) {
    al_ulp_start();
    ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
  }

  // perform sleep (no return)
  esp_deep_sleep_start();
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
