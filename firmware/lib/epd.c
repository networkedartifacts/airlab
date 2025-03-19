#include <naos.h>
#include <naos/sys.h>
#include <string.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>

#include <al/epd.h>

#define AL_EPD_4W false
#define AL_EPD_DC GPIO_NUM_46
#define AL_EPD_RST GPIO_NUM_41
#define AL_EPD_BSY GPIO_NUM_40
#define AL_EPD_SEL GPIO_NUM_42
#define AL_EPD_DEBUG false
#define AL_EPD_OTP_LUT true
#define AL_EPD_BUFFER (AL_EPD_FRAME / 8 * 9 + 2)
#define AL_EPD_SLEEP 5000

// See: https://github.com/ZinggJM/GxEPD2/blob/master/src/epd/GxEPD2_290_T94_V2.cpp.

static const uint8_t al_epd_lut_partial[153] = {
    0x0,  0x40, 0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x80, 0x80, 0x0, 0x0, 0x0, 0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x40, 0x40, 0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0, 0x80, 0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0, 0x0,
    0x0A, 0x0,  0x0, 0x0, 0x0,  0x0,  0x2,  0x1,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x1, 0x0, 0x0, 0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0, 0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0,
};

static const uint8_t al_epd_lut_full[153] = {
    0x80, 0x66, 0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x40, 0x0,  0x0, 0x0, 0x10, 0x66, 0x0, 0x0, 0x0,  0x0,  0x0, 0x0,
    0x20, 0x0,  0x0, 0x0, 0x80, 0x66, 0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x40, 0x0,  0x0, 0x0, 0x10, 0x66, 0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x20, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0,
    0x14, 0x8,  0x0, 0x0, 0x0,  0x0,  0x1,  0xA,  0xA,  0x0,  0xA, 0xA, 0x0,  0x1,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x14, 0x8,  0x0, 0x1,
    0x0,  0x0,  0x1, 0x0, 0x0,  0x0,  0x0,  0x0,  0x0,  0x1,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0, 0x0,  0x0,  0x0, 0x0,
    0x0,  0x0,  0x0, 0x0, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x0, 0x0, 0x0};

static naos_mutex_t al_epd_mutex;
static spi_device_handle_t al_epd_device;
static uint8_t al_epd_buffer[AL_EPD_BUFFER];

/* SPI helper */

void al_epd_transfer(spi_device_handle_t device, uint8_t *buf, size_t sizeBits) {
  // fail with one bit
  if (sizeBits == 1) {
    ESP_ERROR_CHECK(ESP_FAIL);
    return;
  }

  // handle full bytes
  if ((sizeBits % 8) != 1) {
    spi_transaction_t tx = {
        .length = sizeBits,
        .tx_buffer = buf,
    };
    ESP_ERROR_CHECK(spi_device_transmit(device, &tx));
    return;
  }

  /* split transfer to work around ESP32-S3 bug */

  // acquire bus
  ESP_ERROR_CHECK(spi_device_acquire_bus(device, portMAX_DELAY));

  // send all but last 2 bits
  spi_transaction_t transaction1 = {
      .flags = SPI_TRANS_CS_KEEP_ACTIVE,
      .length = sizeBits - 2,
      .tx_buffer = buf,
  };
  ESP_ERROR_CHECK(spi_device_transmit(device, &transaction1));

  // send last 2 bits
  uint8_t lastBits = (buf[(sizeBits / 8) - 1] << 7) | (buf[sizeBits / 8] >> 1);
  spi_transaction_t transaction2 = {
      .length = 2,
      .tx_buffer = &lastBits,
  };
  ESP_ERROR_CHECK(spi_device_transmit(device, &transaction2));

  // release bus
  spi_device_release_bus(device);
}

/* bitmap manipulation */

static bool al_epd_bmp_get(const uint8_t *buf, size_t pos) {
  // get bit
  size_t byte = pos / 8;
  size_t bit = 7 - pos % 8;  // from left

  return buf[byte] & (1 << bit);
}

static void al_epd_bmp_set(uint8_t *buf, size_t pos, bool val) {
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

static void al_epd_bmp_write(uint8_t *buf, size_t pos, uint8_t byte) {
  // write byte bit by bit
  for (size_t i = 0; i < 8; i++) {
    bool bit = al_epd_bmp_get(&byte, i);
    al_epd_bmp_set(buf, pos + i, bit);
  }
}

/* low-level helpers */

static void al_epd_write_buffer(uint8_t cmd, size_t len, const uint8_t *buf) {
  // log commands
  if (AL_EPD_DEBUG) {
    naos_log("al-epd: write cmd=0x%x len=%ld", cmd, len);
  }

  // check length
  if ((1 + len) * (AL_EPD_4W ? 8 : 9) > AL_EPD_BUFFER * 8) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // prepare command
  if (AL_EPD_4W) {
    al_epd_buffer[0] = cmd;
    for (size_t i = 0; i < len; i++) {
      al_epd_buffer[i + 1] = buf[i];
    }
  } else {
    al_epd_bmp_set(al_epd_buffer, 0, 0);
    al_epd_bmp_write(al_epd_buffer, 1, cmd);
    for (size_t i = 0; i < len; i++) {
      al_epd_bmp_set(al_epd_buffer, (i + 1) * 9, 1);
      al_epd_bmp_write(al_epd_buffer, (i + 1) * 9 + 1, buf[i]);
    }
  }

  // run transactions
  if (AL_EPD_4W) {
    ESP_ERROR_CHECK(gpio_set_level(AL_EPD_DC, 0));
    for (size_t i = 0; i < (1 + len); i++) {
      spi_transaction_t tx = {
          .length = 8,
          .tx_buffer = al_epd_buffer + i,
      };
      ESP_ERROR_CHECK(spi_device_transmit(al_epd_device, &tx));
      if (i == 0) {
        ESP_ERROR_CHECK(gpio_set_level(AL_EPD_DC, 1));
      }
    }
  } else {
    al_epd_transfer(al_epd_device, al_epd_buffer, (1 + len) * 9);
  }
}

static void al_epd_write_word(uint8_t cmd, uint8_t n, uint8_t w1, uint8_t w2, uint8_t w3, uint8_t w4) {
  // write variable buffer of max. 4 bytes
  uint8_t buf[4] = {w1, w2, w3, w4};
  al_epd_write_buffer(cmd, n, buf);
}

static void al_epd_wait(const char *task) {
  if (AL_EPD_DEBUG) {
    naos_log("al-epd: waiting for '%s'...", task);
  }

  // wait while busy
  int64_t start = naos_millis();
  while (gpio_get_level(AL_EPD_BSY) > 0) {
    if (start + 15000 < naos_millis()) {
      ESP_ERROR_CHECK(ESP_FAIL);
    } else {
      naos_delay(1);
    }
  }

  if (AL_EPD_DEBUG) {
    naos_log("al-epd: wait for '%s' took %lldms", task, naos_millis() - start);
  }
}

/* high-level helpers */

static void al_epd_reset() {
  if (AL_EPD_DEBUG) {
    naos_log("al-epd: reset");
  }

  // perform hard reset
  ESP_ERROR_CHECK(gpio_set_level(AL_EPD_RST, 0));
  naos_delay(10);
  ESP_ERROR_CHECK(gpio_set_level(AL_EPD_RST, 1));
  naos_delay(10);

  // perform software reset
  al_epd_write_word(0x12, 0, 0, 0, 0, 0);
  al_epd_wait("reset");  // ~2ms

  // set driver output control
  al_epd_write_word(0x01, 3, 0x27, 0x01, 0x00, 0);

  // set data entry sequence (Y+, X+)
  al_epd_write_word(0x11, 1, 0x03, 0, 0, 0);

  // disable border control
  al_epd_write_word(0x3C, 1, 0xC0, 0, 0, 0);

  // set display update control
  al_epd_write_word(0x21, 2, 0x00, 0x80, 0, 0);

  // use internal temperature sensor
  al_epd_write_word(0x18, 1, 0x80, 0, 0, 0);
}

static void al_epd_load_lut(bool partial) {
  // load lookup table from OTP or flash
  if (AL_EPD_OTP_LUT) {
    al_epd_write_word(0x22, 1, partial ? 0x99 : 0x91, 0, 0, 0);
    al_epd_write_word(0x20, 0, 0, 0, 0, 0);
  } else {
    al_epd_write_buffer(0x32, 153, partial ? al_epd_lut_partial : al_epd_lut_full);
  }
  al_epd_wait("load-lut");  // ~1ms
}

static void al_epd_set_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  // convert from exclusive to inclusive
  x2--;
  y2--;

  // set X/Y start/end positions
  al_epd_write_word(0x44, 2, x1 >> 3, x2 >> 3, 0, 0);
  al_epd_write_word(0x45, 4, y1, y1 >> 8, y2, y2 >> 8);
}

static void al_epd_set_pointer(uint16_t x, uint16_t y) {
  // set initial X/Y address
  al_epd_write_word(0x4E, 1, x >> 3, 0, 0, 0);
  al_epd_write_word(0x4F, 2, y, y >> 8, 0, 0);
}

static void al_epd_display_full(uint8_t *data) {
  if (AL_EPD_DEBUG) {
    naos_log("al-epd: display full");
  }

  // write frame to memory area 0
  al_epd_set_area(0, 0, AL_EPD_WIDTH, AL_EPD_HEIGHT);
  al_epd_set_pointer(0, 0);
  al_epd_write_buffer(0x24, AL_EPD_FRAME, data);

  // write frame to memory area 1
  al_epd_set_area(0, 0, AL_EPD_WIDTH, AL_EPD_HEIGHT);
  al_epd_set_pointer(0, 0);
  al_epd_write_buffer(0x26, AL_EPD_FRAME, data);

  // set display update sequence
  al_epd_write_word(0x22, 1, 0xf7, 0, 0, 0);

  // perform display update sequence
  al_epd_write_word(0x20, 0, 0, 0, 0, 0);
  al_epd_wait("full-update");  // ~2058ms
}

static void al_epd_display_partial(uint8_t *data, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  if (AL_EPD_DEBUG) {
    naos_log("al-epd: display partial x1=%ld y1=%ld x2=%ld y2=%ld", x1, y1, x2, y2);
  }

  // calculate size
  size_t size = (x2 - x1) * (y2 - y1) / 8;

  // write memory area 0
  al_epd_set_area(x1, y1, x2, y2);
  al_epd_set_pointer(x1, y1);
  al_epd_write_buffer(0x24, size, data);

  // set display update sequence
  al_epd_write_word(0x22, 1, 0xff, 0, 0, 0);

  // perform display update sequence
  al_epd_write_word(0x20, 0, 0, 0, 0, 0);
  al_epd_wait("partial-update");  // ~507ms

  // write memory area 1
  al_epd_set_area(x1, y1, x2, y2);
  al_epd_set_pointer(x1, y1);
  al_epd_write_buffer(0x26, size, data);

  // write memory area 0
  al_epd_set_area(x1, y1, x2, y2);
  al_epd_set_pointer(x1, y1);
  al_epd_write_buffer(0x24, size, data);
}

static void al_epd_set_sleep() {
  if (AL_EPD_DEBUG) {
    naos_log("al-epd: sleep");
  }

  // set deep sleep
  al_epd_write_word(0x10, 1, 0x01, 0, 0, 0);
}

/* API */

void al_epd_init() {
  // create mutex
  al_epd_mutex = naos_mutex();

  // clear buffer
  memset(al_epd_buffer, 0, sizeof(al_epd_buffer));

  // initialize pins
  gpio_config_t pin = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = BIT64(AL_EPD_SEL) | BIT64(AL_EPD_RST) | BIT64(AL_EPD_DC),
  };
  ESP_ERROR_CHECK(gpio_config(&pin));
  ESP_ERROR_CHECK(gpio_set_level(AL_EPD_SEL, 1));
  ESP_ERROR_CHECK(gpio_set_level(AL_EPD_RST, 1));
  ESP_ERROR_CHECK(gpio_set_level(AL_EPD_DC, AL_EPD_4W ? 1 : 0));
  pin.mode = GPIO_MODE_INPUT;
  pin.pin_bit_mask = BIT64(AL_EPD_BSY);
  ESP_ERROR_CHECK(gpio_config(&pin));
  naos_delay(10);

  // initialize device
  spi_device_interface_config_t dev = {
      .mode = 0,  // CPOL=0, CPHA=0
      .clock_speed_hz = SPI_MASTER_FREQ_8M,
      .spics_io_num = AL_EPD_SEL,
      .flags = SPI_DEVICE_HALFDUPLEX,
      .cs_ena_pretrans = 10,
      .cs_ena_posttrans = 10,
      .queue_size = 1,
  };
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &al_epd_device));
}

void al_epd_set(uint8_t *data, uint16_t x, uint16_t y, bool black) {
  // set pixel
  size_t pos = y * AL_EPD_WIDTH + x;
  al_epd_bmp_set(data, pos, !black);
}

void al_epd_update(uint8_t *frame, bool partial) {
  // lock mutex
  naos_lock(al_epd_mutex);

  // reset display
  al_epd_reset();

  // prepare display
  al_epd_load_lut(partial);

  // update display
  if (partial) {
    al_epd_display_partial(frame, 0, 0, AL_EPD_WIDTH, AL_EPD_HEIGHT);
  } else {
    al_epd_display_full(frame);
  }

  // sleep display
  al_epd_set_sleep();

  // unlock mutex
  naos_unlock(al_epd_mutex);
}
