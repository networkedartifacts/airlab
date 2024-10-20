#include <naos.h>
#include <naos/sys.h>
#include <driver/i2c.h>
#include <driver/rmt_tx.h>

#include "sig.h"

#define CAP_ADDR 0x37
#define CAP_INT GPIO_NUM_11
#define CAP_DEBUG false
#define CAP_DEBUG_SENSOR -1

static naos_mutex_t cap_mutex;
static rmt_channel_handle_t cap_buzzer;
static rmt_encoder_handle_t cap_encoder;
static uint8_t cap_map[8] = {2, 6, 1, 0, 5, 4, 3};
static uint8_t cap_last = 0;
static float cap_delta = 0;

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

static float cap_middle(uint8_t num) {
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

void static cap_buzz(int us) {
  // prepare buzz
  rmt_symbol_word_t items[1] = {
      {
          .level0 = 1,
          .duration0 = (uint16_t)us,
          .level1 = 0,
          .duration1 = 1,
      },
  };

  // perform buzz
  rmt_transmit_config_t cfg = {
      .flags.eot_level = 0,
      .flags.queue_nonblocking = 1,
  };
  ESP_ERROR_CHECK(rmt_transmit(cap_buzzer, cap_encoder, items, sizeof(items), &cfg));
}

static void cap_check() {
  // lock mutex
  naos_lock(cap_mutex);

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

  // check if changed
  if (touches == cap_last) {
    naos_unlock(cap_mutex);
    return;
  }

  // buzz once
  cap_buzz(125);

  // capture and update last touches
  uint8_t last = cap_last;
  cap_last = touches;

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
  if (CAP_DEBUG && CAP_DEBUG_SENSOR >= 0) {
    uint8_t cp = cap_read8(0xdd);     // pF
    uint16_t dc = cap_read16(0xde);   // difference count
    uint16_t bl = cap_read16(0xe0);   // baseline
    uint16_t rc = cap_read16(0xe2);   // raw count
    uint16_t arc = cap_read16(0xe4);  // average raw count
    naos_log("cap: debug pad=%d cp=%d dc=%d bl=%d rc=%d arc=%d", CAP_DEBUG_SENSOR, cp, dc, bl, rc, arc);
  }

  // stop, if no touch
  if (touches == 0) {
    naos_unlock(cap_mutex);
    return;
  }

  // calculate middle
  float middle = cap_middle(touches);
  if (CAP_DEBUG) {
    naos_log("cap: middle=%f", middle);
  }

  // calculate position
  float position = middle / 3 - 1;  // -1 to 1

  // calculate diff
  if (last != 0) {
    float diff = middle - cap_middle(last);
    if (CAP_DEBUG) {
      naos_log("cap: diff=%f", diff);
    }
    cap_delta += diff;
  }

  // unlock mutex
  naos_unlock(cap_mutex);

  // dispatch event
  sig_dispatch((sig_event_t){
      .type = SIG_TOUCH,
      .touch = position,
  });
}

void static cap_signal() {
  // defer check
  naos_defer_isr(cap_check);
}

void static cap_monitor() {
  // lock mutex
  naos_lock(cap_mutex);

  // capture delta
  float delta = cap_delta;
  cap_delta = 0;

  // unlock mutex
  naos_unlock(cap_mutex);

  // stop, if no delta
  if (delta == 0) {
    return;
  }

  // dispatch event
  sig_dispatch((sig_event_t){
      .type = SIG_SCROLL,
      .touch = delta,
  });
}

void cap_init() {
  // create mutex
  cap_mutex = naos_mutex();

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
  if (CAP_DEBUG && CAP_DEBUG_SENSOR >= 0) {
    cap_write8(0x82, cap_map[CAP_DEBUG_SENSOR]);
  }

  // setup interrupt
  gpio_config_t io_cfg = {
      .pin_bit_mask = BIT64(CAP_INT),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };
  ESP_ERROR_CHECK(gpio_config(&io_cfg));
  ESP_ERROR_CHECK(gpio_isr_handler_add(CAP_INT, cap_signal, NULL));

  // setup buzzer
  rmt_tx_channel_config_t rmt_cfg = {
      .gpio_num = GPIO_NUM_5,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 1000 * 1000,  // 1 us
      .mem_block_symbols = 48,
      .trans_queue_depth = 16,
  };
  ESP_ERROR_CHECK(rmt_new_tx_channel(&rmt_cfg, &cap_buzzer));
  ESP_ERROR_CHECK(rmt_enable(cap_buzzer));

  // setup buzzer encoder
  rmt_copy_encoder_config_t enc_cfg = {};
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc_cfg, &cap_encoder));

  // run monitor
  naos_repeat("cap", 300, cap_monitor);
}

void cap_sleep() {
  // lock mutex
  naos_lock(cap_mutex);

  // enter low power mode
  cap_read8(0x86);
  cap_write8(0x86, 7);

  // unlock mutex
  naos_unlock(cap_mutex);
}

void cap_wake() {
  // lock mutex
  naos_lock(cap_mutex);

  // exit low power mode
  cap_read8(0x86);

  // unlock mutex
  naos_unlock(cap_mutex);
}
