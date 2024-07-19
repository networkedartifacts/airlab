#include <naos.h>
#include <naos/sys.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <memory.h>

#include "epd.h"

#define EPD_RST GPIO_NUM_39
#define EPD_BSY GPIO_NUM_38
#define EPD_SEL GPIO_NUM_40
#define EPD_DEBUG false
#define EPD_OTP_LUT true
#define EPD_BUFFER (EPD_FRAME / 8 * 9 + 2)
#define EPD_SLEEP 5000

// See: https://github.com/ZinggJM/GxEPD2/blob/master/src/epd/GxEPD2_290_T94_V2.cpp.

static const uint8_t epd_lut_partial[153] = {
    0x0,  0x40, 0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x80, 0x80, 0x0, 0x0, 0x0, 0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x40, 0x40, 0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0, 0x80, 0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0, 0x0,
    0x0A, 0x0,  0x0, 0x0, 0x0,  0x0,  0x2,  0x1,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x1, 0x0, 0x0, 0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0,
};

static const uint8_t epd_lut_full[153] = {
    0x80, 0x66, 0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x40, 0x0,  0x0, 0x0, 0x10, 0x66, 0x0, 0x0, 0x0,  0x0,  0x0, 0x0,
    0x20, 0x0,  0x0, 0x0, 0x80, 0x66, 0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x40, 0x0,  0x0, 0x0, 0x10, 0x66, 0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x20, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0,
    0x14, 0x8,  0x0, 0x0, 0x0,  0x0,  0x1,  0xA,  0xA,  0x0,  0xA, 0xA, 0x0,  0x1,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x14, 0x8,  0x0, 0x1,
    0x0,  0x0,  0x1, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x1,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x0, 0x0, 0x0};

static naos_mutex_t epd_mutex;
static spi_device_handle_t epd_device;
static bool epd_awake = false;
static uint32_t epd_updated = 0;
static uint8_t *epd_buffer = NULL;
static uint8_t *epd_frame = NULL;

/* bitmap manipulation */

static void epd_bmp_set(uint8_t *buf, size_t pos, bool val) {
  // determine byte and bit
  size_t byte = pos / 8;
  size_t bit = 7 - pos % 8;  // from left

  // set or clear bit
  if (val) {
    buf[byte] |= 1 << bit;
  } else {
    buf[byte] &= ~(1 << bit);
  }
}

static bool epd_bmp_get(const uint8_t *buf, size_t pos) {
  // get bit
  size_t byte = pos / 8;
  size_t bit = 7 - pos % 8;  // from left

  return buf[byte] & (1 << bit);
}

static void epd_bmp_write(uint8_t *buf, size_t pos, uint8_t byte) {
  // write byte bit by bit
  for (size_t i = 0; i < 8; i++) {
    bool bit = epd_bmp_get(&byte, i);
    epd_bmp_set(buf, pos + i, bit);
  }
}

static void epd_bmp_print(const uint8_t *data, size_t length) {
  // print buffer bits MSB first
  printf("[ ");
  for (size_t i = 0; i < length; i++) {
    for (int j = 7; 0 <= j; j--) {
      printf("%c", (data[i] & (1 << j)) ? '1' : '0');
    }
    printf(" ");
  }
  printf("] MSB-FIRST\n");
}

/* low-level helpers */

static void epd_write_buffer(uint8_t cmd, size_t len, const uint8_t *buf) {
  if (EPD_DEBUG) {
    naos_log("epd: write cmd=0x%x len=%ld", cmd, len);
  }

  // check length
  if ((len + 1) * 9 > EPD_BUFFER * 8) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // write command
  epd_bmp_set(epd_buffer, 0, 0);
  epd_bmp_write(epd_buffer, 1, cmd);

  // remap data to 9 bits as 1+byte
  for (size_t i = 0; i < len; i++) {
    epd_bmp_set(epd_buffer, (i + 1) * 9, 1);
    epd_bmp_write(epd_buffer, (i + 1) * 9 + 1, buf[i]);
  }

  // print buffer
  if (EPD_DEBUG) {
    epd_bmp_print(epd_buffer, 16);
  }

  // run transaction
  spi_transaction_t tx = {
      .length = (len + 1) * 9,
      .tx_buffer = epd_buffer,
  };
  ESP_ERROR_CHECK(spi_device_transmit(epd_device, &tx));
}

static void epd_write_word(uint8_t cmd, uint8_t n, uint8_t w1, uint8_t w2, uint8_t w3, uint8_t w4) {
  // write variable buffer of max. 4 bytes
  uint8_t buf[4] = {w1, w2, w3, w4};
  epd_write_buffer(cmd, n, buf);
}

static void epd_wait(uint16_t delay) {
  if (EPD_DEBUG) {
    naos_log("epd: wait...");
  }

  // delay a bit
  if (delay > 0) {
    naos_delay(delay);
  }

  // wait while busy
  uint32_t start = naos_millis();
  while (gpio_get_level(EPD_BSY) > 0) {
    if (start + 5000 < naos_millis()) {
      ESP_ERROR_CHECK(ESP_FAIL);
    } else {
      naos_delay(1);
    }
  }

  if (EPD_DEBUG) {
    naos_log("epd: done!");
  }
}

/* high-level helpers */

static void epd_reset() {
  if (EPD_DEBUG) {
    naos_log("epd: reset");
  }

  // perform hard reset
  ESP_ERROR_CHECK(gpio_set_level(EPD_RST, 0));
  naos_delay(10);
  ESP_ERROR_CHECK(gpio_set_level(EPD_RST, 1));
  naos_delay(10);

  // perform software reset
  epd_write_word(0x12, 0, 0, 0, 0, 0);
  epd_wait(0);  // ~2ms

  // set driver output control
  epd_write_word(0x01, 3, 0x27, 0x01, 0x00, 0);

  // set data entry sequence (Y+, X+)
  epd_write_word(0x11, 1, 0x03, 0, 0, 0);

  // set display update control
  epd_write_word(0x21, 2, 0x00, 0x80, 0, 0);
}

static void epd_prepare(bool partial) {
  // reset (for good quality)
  if (partial) {
    epd_reset();
  }

  // load lookup table from OTP or flash
  if (EPD_OTP_LUT) {
    epd_write_word(0x22, 1, partial ? 0x99 : 0x91, 0, 0, 0);
    epd_write_word(0x20, 0, 0, 0, 0, 0);
  } else {
    epd_write_buffer(0x32, 153, partial ? epd_lut_partial : epd_lut_full);
  }
  epd_wait(0);  // ~1ms
}

static void epd_set_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  // convert from exclusive to inclusive
  x2--;
  y2--;

  // set X/Y start/end positions
  epd_write_word(0x44, 2, x1 >> 3, x2 >> 3, 0, 0);
  epd_write_word(0x45, 4, y1, y1 >> 8, y2, y2 >> 8);
}

static void epd_set_pointer(uint16_t x, uint16_t y) {
  // set initial X/Y address
  epd_write_word(0x4E, 1, x >> 3, 0, 0, 0);
  epd_write_word(0x4F, 2, y, y >> 8, 0, 0);
}

static void epd_display_full(uint8_t *data) {
  if (EPD_DEBUG) {
    naos_log("epd: display full");
  }

  // write frame to memory area 0
  epd_set_area(0, 0, EPD_WIDTH, EPD_HEIGHT);
  epd_set_pointer(0, 0);
  epd_write_buffer(0x24, EPD_FRAME, data);

  // write frame to memory area 1
  epd_set_area(0, 0, EPD_WIDTH, EPD_HEIGHT);
  epd_set_pointer(0, 0);
  epd_write_buffer(0x26, EPD_FRAME, data);

  // set display update sequence
  epd_write_word(0x22, 1, 0xf7, 0, 0, 0);

  // perform display update sequence
  epd_write_word(0x20, 0, 0, 0, 0, 0);
  epd_wait(3260);  // ~3265ms
}

static void epd_display_partial(uint8_t *data, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  if (EPD_DEBUG) {
    naos_log("epd: display partial x1=%ld y1=%ld x2=%ld y2=%ld", x1, y1, x2, y2);
  }

  // calculate size
  size_t size = (x2 - x1) * (y2 - y1) / 8;

  // write memory area 0
  epd_set_area(x1, y1, x2, y2);
  epd_set_pointer(x1, y1);
  epd_write_buffer(0x24, size, data);

  // set display update sequence
  epd_write_word(0x22, 1, 0xff, 0, 0, 0);

  // perform display update sequence
  epd_write_word(0x20, 0, 0, 0, 0, 0);
  epd_wait(590);  // ~594ms

  // write memory area 1
  epd_set_area(x1, y1, x2, y2);
  epd_set_pointer(x1, y1);
  epd_write_buffer(0x26, size, data);

  // write memory area 0
  epd_set_area(x1, y1, x2, y2);
  epd_set_pointer(x1, y1);
  epd_write_buffer(0x24, size, data);
}

static void epd_set_sleep() {
  if (EPD_DEBUG) {
    naos_log("epd: sleep");
  }

  // set deep sleep
  epd_write_word(0x10, 1, 0x01, 0, 0, 0);
}

/* background task */

static void epd_check() {
  // lock mutex
  naos_lock(epd_mutex);

  // sleep display after timeout
  if (epd_awake && epd_updated + EPD_SLEEP < naos_millis()) {
    epd_set_sleep();
    epd_awake = false;
  }

  // unlock mutex
  naos_unlock(epd_mutex);
}

/* API */

void epd_init() {
  // create mutex
  epd_mutex = naos_mutex();

  // allocate buffers
  epd_frame = calloc(EPD_FRAME, sizeof(uint8_t));
  epd_buffer = calloc(EPD_BUFFER, sizeof(uint8_t));

  // initialize pins
  gpio_config_t pin = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = BIT64(EPD_RST),
  };
  ESP_ERROR_CHECK(gpio_config(&pin));
  pin.mode = GPIO_MODE_INPUT;
  pin.pin_bit_mask = BIT64(EPD_BSY);
  ESP_ERROR_CHECK(gpio_config(&pin));

  // initialize device
  spi_device_interface_config_t dev = {
      .mode = 0,  // CPOL=0, CPHA=0
      .clock_speed_hz = SPI_MASTER_FREQ_20M,
      .spics_io_num = EPD_SEL,
      .flags = SPI_DEVICE_HALFDUPLEX,
      .queue_size = 1,
  };
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &epd_device));

  // run periodic check
  naos_repeat("epd", 500, epd_check);
}

void epd_set(uint8_t *data, uint16_t x, uint16_t y, bool black) {
  // set pixel
  size_t pos = y * EPD_WIDTH + x;
  epd_bmp_set(data, pos, !black);
}

void epd_update(uint8_t *data, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, bool partial) {
  // lock mutex
  naos_lock(epd_mutex);

  // round area to 8 pixels
  if (x1 % 8) x1 = x1 / 8 * 8;
  if (y1 % 8) y1 = y1 / 8 * 8;
  if (x2 % 8) x2 = (x2 + 7) / 8 * 8;
  if (y2 % 8) y2 = (y2 + 7) / 8 * 8;

  // update frame
  if (partial) {
    size_t i = 0;
    for (size_t y = y1; y < y2; y++) {
      for (size_t x = x1; x < x2; x++) {
        epd_bmp_set(epd_frame, i++, epd_bmp_get(data, y * EPD_WIDTH + x));
      }
    }
  } else {
    memcpy(epd_frame, data, EPD_FRAME / 8);
  }

  // awake display
  if (!epd_awake) {
    epd_reset();
    epd_awake = true;
  }

  // prepare display
  epd_prepare(partial);

  // update display
  if (partial) {
    epd_display_partial(epd_frame, x1, y1, x2, y2);
  } else {
    epd_display_full(data);
  }

  // set time
  epd_updated = naos_millis();

  // unlock mutex
  naos_unlock(epd_mutex);
}

void epd_sleep() {
  // lock mutex
  naos_lock(epd_mutex);

  // set sleep
  epd_set_sleep();
  epd_awake = false;

  // unlock mutex
  naos_unlock(epd_mutex);
}
