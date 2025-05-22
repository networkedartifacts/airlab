#include <naos.h>
#include <naos/sys.h>
#include <esp_err.h>

#include <al/sensor.h>

#include "sig.h"

#define SIG_DEBUG false

static naos_queue_t sig_queue;

static void sig_sensor(al_sample_t sample) {
  // dispatch event
  sig_dispatch((sig_event_t){
      .type = SIG_SENSOR,
  });
}

void sig_init() {
  // create queue
  sig_queue = naos_queue(3, sizeof(sig_event_t));

  // wire sensor
  al_sensor_config(sig_sensor);
}

void sig_dispatch(sig_event_t event) {
  // safety check
  if (event.type == SIG_ANY) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // add event to queue or drop if full
  if (naos_push(sig_queue, &event, 0)) {
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
    timeout = -1;  // no timeout
  }

  // get deadline
  int64_t deadline = naos_millis() + timeout;

  for (;;) {
    // update timeout
    if (timeout > 0) {
      int64_t now = naos_millis();
      if (now >= deadline) {
        return (sig_event_t){.type = SIG_TIMEOUT};
      }
      timeout = deadline - now;
      if (SIG_DEBUG) {
        naos_log("sig: adjusted timeout %lld", timeout);
      }
    }

    // get next event
    sig_event_t event = {.type = SIG_TIMEOUT};
    naos_pop(sig_queue, &event, (int32_t)timeout);
    if (SIG_DEBUG) {
      naos_log("sig: dequeued %d (timeout=%lld)", event.type, timeout);
    }

    // apply filter if provided
    if (filter != SIG_ANY && (event.type & filter) == 0) {
      if (SIG_DEBUG) {
        naos_log("sig: skipping %d", event.type);
      }
      continue;
    }

    return event;
  }
}
