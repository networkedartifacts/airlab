#include <esp_random.h>

#include "stm.h"
#include "sns.h"

stm_entry_t stm_entries[] = {
    {
        .text = "Guten Morgen! Heute wird ein herrlicher Tag!",
        .exclaim = true,
        .action = STM_FROM_INTRO,
    },
    {
        .text = "Ich kann hier kaum atmen!",
        .exclaim = true,
        .co2_min = 2000,
    },
    {
        .text = "Cool, du hast deine Messung abgeschlossen!",
        .exclaim = true,
        .action = STM_COMP_MEASUREMENT,
    },
    {
        .text = "Ahhh... Ich liebe frische Luft!",
        .exclaim = false,
        .co2_max = 600,
    },
    {
        .text = "Ich bin grad am rechnen...",
        .exclaim = false,
        .action = STM_FROM_MEASUREMENT,
    },
    {
        .text = "Warme Luft kann mehr Feuchtigkeit aufnehmen als kalte Luft.",
        .exclaim = false,
    },
};

size_t stm_num = sizeof(stm_entries) / sizeof(stm_entry_t);

stm_entry_t* stm_get(size_t i) { return i < stm_num ? &stm_entries[i] : NULL; }

stm_entry_t* stm_query(bool exclaim, stm_action_t action) {
  // get sensor data
  sns_state_t sensor = sns_get();

  // de/select and count entries
  int selected = 0;
  for (size_t i = 0; i < stm_num; i++) {
    // get entry
    stm_entry_t* entry = &stm_entries[i];

    // set selection
    entry->selected = true;

    // check exclaim
    if (entry->exclaim != exclaim) {
      entry->selected = false;
    }

    // check action
    if (entry->action != 0 && action != entry->action) {
      entry->selected = false;
    }

    // check co2
    if (entry->co2_min != 0 && sensor.co2 < entry->co2_min) {
      entry->selected = false;
    } else if (entry->co2_max != 0 && sensor.co2 > entry->co2_max) {
      entry->selected = false;
    }

    // check temperature
    if (entry->tmp_min != 0 && sensor.tmp < entry->tmp_min) {
      entry->selected = false;
    } else if (entry->tmp_max != 0 && sensor.tmp > entry->tmp_max) {
      entry->selected = false;
    }

    // check humidity
    if (entry->hum_min != 0 && sensor.hum < entry->hum_min) {
      entry->selected = false;
    } else if (entry->hum_max != 0 && sensor.hum > entry->hum_max) {
      entry->selected = false;
    }

    // increment if selected
    if (entry->selected) {
      selected++;
    }
  }

  // check selected
  if (selected == 0) {
    return NULL;
  }

  // choose entry randomly
  selected = (int)esp_random() % selected;

  // find and return entry
  for (int i = 0; i < stm_num; i++) {
    stm_entry_t* entry = &stm_entries[i];
    if (entry->selected) {
      selected--;
      if (selected < 0) {
        return entry;
      }
    }
  }

  return NULL;
}
