#include <esp_random.h>
#include <math.h>

#include <al/sensor.h>
#include <al/store.h>

#include "stm.h"

#include "stm_data.inc"

int stm_num() {
  // return number of entries
  return sizeof(stm_entries) / sizeof(stm_entry_t);
}

stm_entry_t* stm_get(size_t i) {
  // return entry by index
  return i < stm_num() ? &stm_entries[i] : NULL;
}

stm_entry_t* stm_query(bool urgent, stm_action_t action) {
  // get last sample
  al_sample_t sample = al_store_last();

  // check if ok
  bool ok = al_sample_valid(sample);

  // calculate values
  float co2 = al_sample_read(sample, AL_SAMPLE_CO2);
  float tmp = al_sample_read(sample, AL_SAMPLE_TMP);
  float hum = al_sample_read(sample, AL_SAMPLE_HUM);
  float voc = al_sample_read(sample, AL_SAMPLE_VOC);
  float nox = al_sample_read(sample, AL_SAMPLE_NOX);
  float pm = al_sample_read(sample, AL_SAMPLE_PM);

  // treat PM readings without an installed sensor or with an obstruction as unavailable
  bool has_pm = al_sensor_pm_present();
  bool pm_ok = has_pm && !isnan(pm) && (al_sample_flags(sample, AL_SAMPLE_PM) & AL_SAMPLE_PM_OBSTRUCTED) == 0;

  // de/select and count entries
  int selected = 0;
  for (size_t i = 0; i < stm_num(); i++) {
    // get entry
    stm_entry_t* entry = &stm_entries[i];

    // set selection
    entry->selected = true;

    // check urgency
    if (entry->urgent != urgent) {
      entry->selected = false;
      continue;
    }

    // check action
    if (entry->action != 0 && action != entry->action) {
      entry->selected = false;
      continue;
    }

    // check co2
    if (entry->co2_min != 0 && (!ok || co2 < entry->co2_min)) {
      entry->selected = false;
      continue;
    }
    if (entry->co2_max != 0 && (!ok || co2 > entry->co2_max)) {
      entry->selected = false;
      continue;
    }

    // check temperature
    if (entry->tmp_min != 0 && (!ok || tmp < entry->tmp_min)) {
      entry->selected = false;
      continue;
    }
    if (entry->tmp_max != 0 && (!ok || tmp > entry->tmp_max)) {
      entry->selected = false;
      continue;
    }

    // check humidity
    if (entry->hum_min != 0 && (!ok || hum < entry->hum_min)) {
      entry->selected = false;
      continue;
    }
    if (entry->hum_max != 0 && (!ok || hum > entry->hum_max)) {
      entry->selected = false;
      continue;
    }

    // check VOC (NaN means no reading is available)
    if (entry->voc_min != 0 && (!ok || isnan(voc) || voc < entry->voc_min)) {
      entry->selected = false;
      continue;
    }
    if (entry->voc_max != 0 && (!ok || isnan(voc) || voc > entry->voc_max)) {
      entry->selected = false;
      continue;
    }

    // check NOx (NaN means no reading is available)
    if (entry->nox_min != 0 && (!ok || isnan(nox) || nox < entry->nox_min)) {
      entry->selected = false;
      continue;
    }
    if (entry->nox_max != 0 && (!ok || isnan(nox) || nox > entry->nox_max)) {
      entry->selected = false;
      continue;
    }

    // check PM sensor requirement
    if (entry->needs_pm && !has_pm) {
      entry->selected = false;
      continue;
    }

    // check PM (no sensor, no reading or obstruction means unavailable)
    if (entry->pm_min != 0 && (!ok || !pm_ok || pm < entry->pm_min)) {
      entry->selected = false;
      continue;
    }
    if (entry->pm_max != 0 && (!ok || !pm_ok || pm > entry->pm_max)) {
      entry->selected = false;
      continue;
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
  for (int i = 0; i < stm_num(); i++) {
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
