#include <naos.h>
#include <naos_sys.h>

#include "sig.h"

#define SIG_DEBUG false

static QueueHandle_t sig_queue;

void sig_init() {
  // create queue
  sig_queue = xQueueCreate(3, sizeof(sig_event_t));
}

void sig_dispatch(sig_event_t event) {
  // safety check
  if (event == SIG_ANY) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // add event to queue or drop if full
  if (xQueueSendToBack(sig_queue, &event, 0)) {
    if (SIG_DEBUG) {
      naos_log("sig: queued %d", event);
    }
  } else {
    if (SIG_DEBUG) {
      naos_log("sig: dropped: %d", event);
    }
  }
}

sig_event_t sig_await(sig_event_t filter, int64_t timeout) {
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
    sig_event_t event = SIG_TIMEOUT;
    xQueueReceive(sig_queue, &event, timeout / portTICK_PERIOD_MS);
    if (SIG_DEBUG) {
      naos_log("sig: dequeued %d", event);
    }

    // apply filter if provided
    if (filter != SIG_ANY && (event & filter) == 0) {
      if (SIG_DEBUG) {
        naos_log("sig: skipping %d", event);
      }

      // update timeout
      timeout = deadline - naos_millis();
      if (timeout < 0) {
        return SIG_TIMEOUT;
      }

      continue;
    }

    return event;
  }
}
