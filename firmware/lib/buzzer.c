#include <naos.h>
#include <naos/sys.h>
#include <driver/ledc.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define BUZZER_DEBUG false

// Component: PKMCS0909E

static naos_mutex_t al_buzzer_mutex;
static naos_signal_t al_buzzer_signal;
static esp_timer_handle_t al_buzzer_timer;

static void al_buzzer_done() {
  // stop channels
  ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0));
  ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 1));

  // signal done
  naos_trigger(al_buzzer_signal, 1, false);
}

static void al_buzzer_tone(int hz, int us) {
  // acquire mutex
  if (!xSemaphoreTake(al_buzzer_mutex, pdMS_TO_TICKS(10))) {
    if (BUZZER_DEBUG) {
      naos_log("al-bzr: busy");
    }
    return;
  }

  // log
  if (BUZZER_DEBUG) {
    naos_log("al-bzr: tone hz=%d us=%d", hz, us);
  }

  // clear signal
  naos_trigger(al_buzzer_signal, 1, true);

  // start beep
  ESP_ERROR_CHECK(ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, hz));
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512));
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 512));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));

  // start timer
  ESP_ERROR_CHECK(esp_timer_start_once(al_buzzer_timer, us));

  // calculate timeout
  int32_t timeout = 5;
  if (us > 1000) {
    timeout += us / 1000;
  }

  // await signal
  naos_await(al_buzzer_signal, 1, true, timeout);

  // ensure timer is stopped
  esp_timer_stop(al_buzzer_timer);

  // ensure channels are stopped
  ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0));
  ESP_ERROR_CHECK(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 1));

  // release mutex
  naos_unlock(al_buzzer_mutex);
}

void al_buzzer_init() {
  // prepare state
  al_buzzer_mutex = naos_mutex();
  al_buzzer_signal = naos_signal();

  // setup LEDC timer
  ledc_timer_config_t ledc_timer = {
      .freq_hz = 440,
      .duty_resolution = LEDC_TIMER_10_BIT,
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

void al_buzzer_click() {
  // make tone
  al_buzzer_tone(8000, 125);
}

void al_buzzer_beep(int hz, int ms) {
  // check arguments
  if (hz == 0 || ms == 0) {
    return;
  }

  // make tone
  al_buzzer_tone(hz, ms * 1000);
}
