#include <naos.h>
#include <naos/sys.h>

#include <al/sensor.h>

#include "sig.h"

#define SIG_DEBUG false

static QueueHandle_t sig_queue;

static void sig_sensor(al_sample_t) {
  // dispatch event
  sig_dispatch((sig_event_t){
      .type = SIG_SENSOR,
  });
}

void sig_init() {
  // create queue
  sig_queue = xQueueCreate(3, sizeof(sig_event_t));

  // wire sensor
  al_sensor_config(sig_sensor);
}

void sig_dispatch(sig_event_t event) {
  // safety check
  if (event.type == SIG_ANY) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // add event to queue or drop if full
  if (xQueueSendToBack(sig_queue, &event, 0)) {
    if (SIG_DEBUG) {
      naos_log("sig: queued %d", event.type);
    }
  } else {
    if (SIG_DEBUG) {
      naos_log("sig: dropped: %d", event.type);
    }
  }
}

sig_event_t sig_await(sig_type_t filter, int64_t timeout) {
  // handle timeout
  if (timeout > 0) {
    if (filter != SIG_ANY) {
      filter |= SIG_TIMEOUT;
    }
  } else {
    timeout = portMAX_DELAY;
  }

  // get deadline
  int64_t deadline = naos_millis() + timeout;

  for (;;) {
    // get next event
    sig_event_t event = {.type = SIG_TIMEOUT};
    xQueueReceive(sig_queue, &event, timeout / portTICK_PERIOD_MS);
    if (SIG_DEBUG) {
      naos_log("sig: dequeued %d", event.type);
    }

    // apply filter if provided
    if (filter != SIG_ANY && (event.type & filter) == 0) {
      if (SIG_DEBUG) {
        naos_log("sig: skipping %d", event.type);
      }

      // update timeout
      timeout = deadline - naos_millis();
      if (timeout < 0) {
        event.type = SIG_TIMEOUT;
        return event;
      }

      continue;
    }

    return event;
  }
}
