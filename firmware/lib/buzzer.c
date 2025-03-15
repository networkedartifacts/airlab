#include <driver/rmt_tx.h>

static rmt_channel_handle_t al_buzzer_channel;
static rmt_encoder_handle_t al_buzzer_encoder;

void al_buzzer_init() {
  // setup buzzer
  rmt_tx_channel_config_t rmt_cfg = {
      .gpio_num = GPIO_NUM_5,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 1000 * 1000,  // 1 us
      .mem_block_symbols = 48,
      .trans_queue_depth = 16,
  };
  ESP_ERROR_CHECK(rmt_new_tx_channel(&rmt_cfg, &al_buzzer_channel));
  ESP_ERROR_CHECK(rmt_enable(al_buzzer_channel));

  // setup buzzer encoder
  rmt_copy_encoder_config_t enc_cfg = {};
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc_cfg, &al_buzzer_encoder));
}

void al_buzzer_click() {
  // prepare buzz
  rmt_symbol_word_t item = {
      .level0 = 1,
      .duration0 = 125,  // us
      .level1 = 0,
      .duration1 = 1,
  };

  // perform buzz
  rmt_transmit_config_t cfg = {
      .flags.eot_level = 0,
      .flags.queue_nonblocking = 1,
  };
  ESP_ERROR_CHECK(rmt_transmit(al_buzzer_channel, al_buzzer_encoder, &item, sizeof(item), &cfg));
}

void al_buzzer_beep(int hz, int ms) {
  // check arguments
  if (hz == 0 || ms == 0) {
    return;
  }

  // calculate parameters
  int period_us = 1000000 / hz;
  int cycles = (ms * 1000) / period_us;

  // generate waveform
  rmt_symbol_word_t waveform[cycles];
  for (int i = 0; i < cycles; i++) {
    waveform[i] = (rmt_symbol_word_t){
        .level0 = 1,
        .duration0 = period_us / 2,
        .level1 = 0,
        .duration1 = period_us / 2,
    };
  }

  // perform tone
  rmt_transmit_config_t cfg = {
      .flags.eot_level = 0,
      .flags.queue_nonblocking = 1,
  };
  ESP_ERROR_CHECK(rmt_transmit(al_buzzer_channel, al_buzzer_encoder, waveform, sizeof(waveform), &cfg));
}
