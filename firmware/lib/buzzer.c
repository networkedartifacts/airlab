#include <naos.h>
#include <naos/sys.h>
#include <driver/ledc.h>
#include <esp_timer.h>

#define BUZZER_DEBUG false

// Component: PKMCS0909E

static naos_signal_t al_buzzer_signal;
static naos_queue_t al_buzzer_queue;
static esp_timer_handle_t al_buzzer_timer;
static bool al_buzzer_ready = false;
static bool al_buzzer_wait = false;

static void al_buzzer_done() {
  // stop channels
  ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0));
  ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 1));

  // signal done
  if (al_buzzer_wait) {
    naos_trigger(al_buzzer_signal, 1, false);
  } else {
    naos_push(al_buzzer_queue, NULL, 0);
  }
}

static void al_buzzer_setup() {
  // setup LEDC timer
  ledc_timer_config_t ledc_timer = {
      .freq_hz = 440,
      .duty_resolution = LEDC_TIMER_12_BIT,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .timer_num = LEDC_TIMER_0,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

  // setup LEDC channels
  ledc_channel_config_t ch1 = {
      .channel = LEDC_CHANNEL_0,
      .duty = 0,
      .gpio_num = GPIO_NUM_5,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .hpoint = 0,
      .timer_sel = LEDC_TIMER_0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&ch1));
  ledc_channel_config_t ch2 = {
      .channel = LEDC_CHANNEL_1,
      .duty = 0,
      .gpio_num = GPIO_NUM_46,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .hpoint = 0,
      .timer_sel = LEDC_TIMER_0,
      .flags.output_invert = true,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&ch2));

  // stop LEDC channels
  ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0));
  ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 1));

  // create high-res timer
  esp_timer_create_args_t args = {
      .callback = al_buzzer_done,
      .dispatch_method = ESP_TIMER_TASK,
  };
  ESP_ERROR_CHECK(esp_timer_create(&args, &al_buzzer_timer));
}

static void al_buzzer_tone(int hz, int us, bool wait) {
  // check frequency
  if (hz < 20 || hz > 12000) {
    return;
  }

  // acquire token
  if (!naos_pop(al_buzzer_queue, NULL, 10)) {
    if (BUZZER_DEBUG) {
      naos_log("al-bzr: busy");
    }
    return;
  }

  // setup if needed
  if (!al_buzzer_ready) {
    al_buzzer_setup();
    al_buzzer_ready = true;
  }

  // log
  if (BUZZER_DEBUG) {
    naos_log("al-bzr: tone hz=%d us=%d", hz, us);
  }

  // clear signal
  naos_trigger(al_buzzer_signal, 1, true);

  // start beep
  ESP_ERROR_CHECK(ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, hz));
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1 << 11));
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 1 << 11));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));

  // start timer
  ESP_ERROR_CHECK(esp_timer_start_once(al_buzzer_timer, us));

  // update flag
  al_buzzer_wait = wait;

  // stop if not waiting
  if (!wait) {
    return;
  }

  // await signal
  naos_await(al_buzzer_signal, 1, true, -1);

  // ensure channels are stopped
  ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0));
  ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 1));

  // release token
  naos_push(al_buzzer_queue, NULL, 0);
}

void al_buzzer_init() {
  // prepare state
  al_buzzer_signal = naos_signal();
  al_buzzer_queue = naos_queue(1, 0);

  // add token
  naos_push(al_buzzer_queue, NULL, 0);
}

void al_buzzer_click() {
  // make tone
  al_buzzer_tone(8000, 125, false);
}

void al_buzzer_beep(int hz, int ms, bool wait) {
  // check arguments
  if (hz == 0 || ms == 0) {
    return;
  }

  // make tone
  al_buzzer_tone(hz, ms * 1000, wait);
}
