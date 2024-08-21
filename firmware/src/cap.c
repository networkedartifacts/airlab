#include <naos.h>
#include <naos/sys.h>
#include <driver/i2c.h>

#define CAP_ADDR 0x37
#define CAP_INT GPIO_NUM_11
#define CAP_DEBUG false
#define CAP_DEBUG_SENSOR 0

static uint8_t cap_map[8] = {2, 6, 1, 0, 5, 4, 3};

static void cap_read(uint8_t reg, uint8_t *buf, size_t len) {
  // read data
  for (size_t i = 0; i < 10; i++) {
    esp_err_t err = i2c_master_write_read_device(I2C_NUM_0, CAP_ADDR, &reg, 1, buf, len, 1000);
    if (err != ESP_FAIL) {
      ESP_ERROR_CHECK(err);
      return;
    }
    if (CAP_DEBUG) {
      naos_log("cap: retrying read...");
    }
    naos_delay(100);
  }
  ESP_ERROR_CHECK(ESP_FAIL);
}

static uint8_t cap_read8(uint8_t reg) {
  // read register
  uint8_t value;
  cap_read(reg, &value, 1);

  return value;
}

static uint16_t cap_read16(uint8_t reg) {
  // read register
  uint8_t data[2];
  cap_read(reg, data, 2);

  return data[0] << 8 | data[1];
}

static void cap_write(uint8_t reg, const uint8_t *buf, size_t len) {
  static uint8_t data[32];

  // copy data
  data[0] = reg;
  for (size_t i = 0; i < len; i++) {
    data[i + 1] = buf[i];
  }

  // read data
  ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, CAP_ADDR, data, 1 + len, 1000));
}

static void cap_write8(uint8_t reg, uint8_t value) {
  // write register
  cap_write(reg, &value, 1);
}

static void cap_exec(uint8_t cmd) {
  // check command control
  uint8_t ctrl = cap_read8(0x86);
  if (ctrl != 0) {
    naos_log("cap: busy ctrl=%02x", ctrl);
    ESP_ERROR_CHECK(ESP_FAIL);
    return;
  }

  // execute command
  if (CAP_DEBUG) {
    naos_log("cap: exec cmd=%02x", cmd);
  }
  cap_write8(0x86, cmd);

  // wait
  while (cap_read8(0x86) != 0) {
    if (CAP_DEBUG) {
      naos_log("cap: wait...");
    }
    naos_delay(1);
  }
  if (CAP_DEBUG) {
    naos_log("cap: done");
  }

  // check error
  uint8_t failed = cap_read8(0x88) & 0x01;
  uint8_t code = cap_read8(0x89);
  if (failed) {
    naos_log("cap: failed code=%02x", code);
    ESP_ERROR_CHECK(ESP_FAIL);
  }
}

float cap_middle(uint8_t num) {
  // prepare state
  int start = -1, end = -1;
  int cur_len = 0, max_len = 0;
  int tmp_start = -1;

  // find continuous bits
  for (int i = 0; i < 8; i++) {
    if (num & (1 << i)) {
      if (tmp_start == -1) {
        tmp_start = i;
      }
      cur_len++;
    } else {
      if (cur_len > max_len) {
        max_len = cur_len;
        start = tmp_start;
        end = i - 1;
      }
      tmp_start = -1;
      cur_len = 0;
    }
  }

  // handle last bit
  if (cur_len > max_len) {
    start = tmp_start;
    end = 8 - 1;
  }

  // check if no bits
  if (start == -1) {
    return -1;
  }

  return (float)(start + end) / 2.0f;
}

void cap_check() {
  // read touches
  uint8_t touches;
  cap_read(0xAA, &touches, 1);

  // re-map touches
  uint8_t mapped = 0;
  for (size_t i = 0; i < 7; i++) {
    if (touches & (1 << i)) {
      mapped |= (1 << cap_map[i]);
    }
  }
  touches = mapped;

  // log touches
  if (CAP_DEBUG) {
    uint8_t t1 = touches & 0x01;
    uint8_t t2 = (touches >> 1) & 0x01;
    uint8_t t3 = (touches >> 2) & 0x01;
    uint8_t t4 = (touches >> 3) & 0x01;
    uint8_t t5 = (touches >> 4) & 0x01;
    uint8_t t6 = (touches >> 5) & 0x01;
    uint8_t t7 = (touches >> 6) & 0x01;
    naos_log("cap: touches %d %d %d %d %d %d %d", t1, t2, t3, t4, t5, t6, t7);
  }

  // read debug status
  if (CAP_DEBUG) {
    uint8_t cp = cap_read8(0xdd);  // pF
    uint16_t dc = cap_read16(0xde);
    uint16_t bl = cap_read16(0xe0);
    uint16_t rc = cap_read16(0xe2);
    uint16_t arc = cap_read16(0xe4);
    naos_log("cap: debug cp=%d dc=%d bl=%d rc=%d arc=%d", cp, dc, bl, rc, arc);
  }

  // calculate position
  // naos_log("cap: middle=%f", cap_middle(touches));
}

void static cap_signal() {
  // defer check
  naos_defer_isr(cap_check);
}

void cap_init() {
  // await device
  cap_read8(0x86);

  // reset device
  cap_exec(255);

  // set sensitivity
  cap_write8(0x08, 0xFF);
  cap_write8(0x09, 0xFF);

  // increase thresholds
  cap_write8(0x0c, 196);
  cap_write8(0x0d, 196);
  cap_write8(0x0e, 196);
  cap_write8(0x0f, 196);
  cap_write8(0x10, 196);
  cap_write8(0x11, 196);
  cap_write8(0x12, 196);

  // configure CS7 as shield (SPO1) and HI as interrupt (SPO0)
  cap_write8(0x4c, 0b00100100);

  // enable shield
  cap_write8(0x4f, 0b00000001);

  // enable sensors
  cap_write8(0x00, 0b01111111);

  // calculate checksum
  cap_exec(3);

  // copy checksum
  uint8_t checksum[2];
  cap_read(0x94, checksum, 2);
  cap_write(0x7e, checksum, 2);

  // store configuration
  cap_exec(2);

  // reset device
  cap_exec(255);

  // set debug sensor
  if (CAP_DEBUG) {
    cap_write8(0x82, cap_map[CAP_DEBUG_SENSOR]);
  }

  // setup interrupt
  gpio_config_t cfg = {
      .pin_bit_mask = BIT64(CAP_INT),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));
  ESP_ERROR_CHECK(gpio_isr_handler_add(CAP_INT, cap_signal, NULL));
}
