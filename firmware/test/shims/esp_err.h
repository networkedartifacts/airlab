#ifndef ESP_ERR_H
#define ESP_ERR_H

#include <stdio.h>
#include <stdlib.h>

// host shim for lib code under test

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_TIMEOUT 0x107

// tests may arm a number of expected errors to exercise error paths that
// would otherwise abort, and assert the counter returned to zero afterwards
static inline int *esp_err_expected(void) {
  static int expected = 0;
  return &expected;
}

#define ESP_ERROR_CHECK(x)                                                              \
  do {                                                                                  \
    esp_err_t err_rc_ = (esp_err_t)(x);                                                 \
    if (err_rc_ != ESP_OK) {                                                            \
      if (*esp_err_expected() > 0) {                                                    \
        (*esp_err_expected())--;                                                        \
      } else {                                                                          \
        fprintf(stderr, "ESP_ERROR_CHECK failed: %d (%s:%d)\n", err_rc_, __FILE__, __LINE__); \
        abort();                                                                        \
      }                                                                                 \
    }                                                                                   \
  } while (0)

#endif  // ESP_ERR_H
