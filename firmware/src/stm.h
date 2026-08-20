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
  STM_DEL_MEASUREMENT = 8,
} stm_action_t;

typedef enum {
  STM_HAPPY,
  STM_COLD,
  STM_ANGRY1,
  STM_ANGRY2,
  STM_STANDING,
  STM_POINTING,
  STM_WORKING,
} stm_mood_t;

typedef struct {
  bool urgent;
  stm_action_t action;
  float co2_min;
  float co2_max;
  float tmp_min;
  float tmp_max;
  float hum_min;
  float hum_max;
  float voc_min;
  float voc_max;
  float nox_min;
  float nox_max;
  float pm_min;
  float pm_max;
  bool needs_pm;
  const char *text_de;
  const char *text_en;
  const char *text_es;
  stm_mood_t mood;
  // ---
  bool selected;
} stm_entry_t;

int stm_num();
stm_entry_t *stm_get(size_t i);
stm_entry_t *stm_query(bool urgent, stm_action_t action);

#endif  // STM_H
