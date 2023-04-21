#ifndef STM_H
#define STM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
  STM_FROM_INTRO = 1,
  STM_FROM_SETTINGS = 2,
  STM_FROM_ANALYSIS = 3,
  STM_FROM_MEASUREMENT = 4,
  STM_START_MEASUREMENT = 5,
  STM_START_FIRST_MEASUREMENT = 6,
  STM_COMP_MEASUREMENT = 7,
} stm_action_t;

typedef struct {
  const char *text;
  bool exclaim;
  stm_action_t action;
  uint16_t co2_min;
  uint16_t co2_max;
  float tmp_min;
  float tmp_max;
  float hum_min;
  float hum_max;
  // ---
  bool selected;
} stm_entry_t;

stm_entry_t *stm_get(size_t i);
stm_entry_t *stm_query(bool exclaim, stm_action_t action);

#endif  // STM_H
