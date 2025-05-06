#include <esp_random.h>

#include <al/sensor.h>

#include "stm.h"

stm_entry_t stm_entries[] = {
    /* Exclaims */
    {
        .text_de = "Willkommen im Air Lab! Sieh dich doch einmal um. Berühren ist erlaubt!",
        .text_en = "Welcome to the Air Lab! Please look around. Touching is allowed!",
        .mood = STM_HAPPY,
        .exclaim = true,
        .action = STM_FROM_INTRO,
    },
    {
        .text_de = "Schmuuhhh, mir wird ganz schwindelig...",
        .text_en = "Ughhh, I'm getting dizzy...",
        .mood = STM_COLD,
        .exclaim = true,
        .co2_min = 4000,
    },
    {
        .text_de = "Mir ist übel!",
        .text_en = "I feel sick!",
        .mood = STM_ANGRY1,
        .exclaim = true,
        .co2_min = 3000,
    },
    {
        .text_de = "Ich kann hier kaum atmen!",
        .text_en = "I can hardly breathe here!",
        .mood = STM_ANGRY2,
        .exclaim = true,
        .co2_min = 2000,
        .co2_max = 3000,
    },
    {
        .text_de = "Uuuu... ich bin müde.",
        .text_en = "Ugh... I'm tired.",
        .mood = STM_STANDING,
        .exclaim = true,
        .co2_min = 1700,
        .co2_max = 2000,
    },
    {
        .text_de = "Jo, ich kann mich voll nicht konzentrieren.",
        .text_en = "Yo, I can't concentrate at all.",
        .mood = STM_ANGRY2,
        .exclaim = true,
        .co2_min = 1400,
        .co2_max = 1700,
    },
    {
        .text_de = "Boa ey, ist das kalt hier!",
        .text_en = "Uff, it's cold in here!",
        .mood = STM_COLD,
        .exclaim = true,
        .tmp_max = 10,
    },
    {
        .text_de = "Sag mal, sind wir im Regenwald?",
        .text_en = "Hey, are we in the rainforest?",
        .mood = STM_ANGRY1,
        .exclaim = true,
        .tmp_min = 25,
        .hum_min = 70,
    },
    {
        .text_de = "Ahem, mega trocken hier!",
        .text_en = "Ahem, super dry here!",
        .mood = STM_COLD,
        .exclaim = true,
        .hum_max = 30,
    },
    {
        .text_de = "Cool, du hast deine Messung abgeschlossen!",
        .text_en = "Cool, you've completed your measurement!",
        .mood = STM_HAPPY,
        .exclaim = true,
        .action = STM_COMP_MEASUREMENT,
    },
    {
        .text_de = "Die Analyse ist der Anfang der Erkenntnis.",
        .text_en = "Analysis is the beginning of knowledge.",
        .mood = STM_POINTING,
        .exclaim = true,
        .action = STM_FROM_ANALYSIS,
    },
    {
        .text_de = "Super, du hast gerade deine erste Messung gestartet!",
        .text_en = "Great, you've just started your first measurement!",
        .mood = STM_POINTING,
        .exclaim = true,
        .action = STM_START_FIRST_MEASUREMENT,
    },
    /* Fun Facts */
    {
        .text_de = "Ahhh... Ich liebe frische Luft!",
        .text_en = "Ahhh... I love fresh air!",
        .mood = STM_HAPPY,
        .co2_max = 600,
    },
    {
        .text_de = "Die Luft hier ist jetzt richtig nice!",
        .text_en = "The air here is really nice now!",
        .mood = STM_HAPPY,
        .co2_max = 600,
        .hum_min = 40,
        .hum_max = 60,
    },
    {
        .text_de = "Warme Luft kann mehr Feuchtigkeit auf- nehmen als kalte Luft.",
        .text_en = "Warm air can absorb more moisture than cold air.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Pro Tag atmen wir 10'000 bis 20'000 Liter Luft.",
        .text_en = "We breathe 10,000 to 20,000 liters of air per day.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Die Luft ist ein Gemisch aus vielen verschiedenen Gasen.",
        .text_en = "The air is a mixture of many different gases.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "78% der Luft ist Stickstoff.",
        .text_en = "78% of the air is nitrogen.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "21% der Luft ist Sauerstoff.",
        .text_en = "21% of the air is oxygen.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Achtung! Luftschad- stoffe können dich krank machen!",
        .text_en = "Attention! Air pollutants can make you sick!",
        .mood = STM_POINTING,
    },
    {
        .text_de = "1% der Luft sind ganz viele verschiedene Spurengase.",
        .text_en = "1% of the air are many different trace gases.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Klug ist jener, der Schweres einfach sagt.",
        .text_en = "Wise is the one who says heavy things simply.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Auch Bakterien und Viren fliegen in der Luft herum!",
        .text_en = "Bacteria and viruses are also flying around in the air!",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Pro Tag atmest du ca. 2'500 Liter CO2 aus!",
        .text_en = "You exhale about 2,500 liters of CO2 per day!",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Ein Kubikmeter CO2 wiegt 1.98 kg.",
        .text_en = "One cubic meter of CO2 weighs 1.98 kg.",
        .mood = STM_POINTING,
    },
    {
        .text_de = "Ich bin am rechnen...",
        .text_en = "I'm calculating...",
        .mood = STM_WORKING,
        .action = STM_FROM_MEASUREMENT,
    },
    {
        .text_de = "Hmmm...",
        .text_en = "Hmmm...",
        .mood = STM_WORKING,
        .action = STM_FROM_MEASUREMENT,
    },
    {
        .text_de = "Ich mache gerade eine wichtige Messung...",
        .text_en = "I'm taking an important measurement...",
        .mood = STM_WORKING,
        .action = STM_FROM_MEASUREMENT,
    },
    {
        .text_de = "(RH + 454) x 10^3/0.544 = ?",
        .text_en = "(RH + 454) x 10^3/0.544 = ?",
        .mood = STM_WORKING,
        .action = STM_FROM_MEASUREMENT,
    },
};

size_t stm_num = sizeof(stm_entries) / sizeof(stm_entry_t);

stm_entry_t* stm_get(size_t i) { return i < stm_num ? &stm_entries[i] : NULL; }

stm_entry_t* stm_query(bool exclaim, stm_action_t action) {
  // get last sample
  al_sample_t sample = al_sensor_last();

  // check if ok
  bool ok = al_sample_valid(sample);

  // de/select and count entries
  int selected = 0;
  for (size_t i = 0; i < stm_num; i++) {
    // get entry
    stm_entry_t* entry = &stm_entries[i];

    // calculate values
    float co2 = al_sample_read(sample, AL_SAMPLE_CO2);
    float tmp = al_sample_read(sample, AL_SAMPLE_TMP);
    float hum = al_sample_read(sample, AL_SAMPLE_HUM);

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
    if (entry->co2_min != 0 && (!ok || co2 < entry->co2_min)) {
      entry->selected = false;
    } else if (entry->co2_max != 0 && (!ok || co2 > entry->co2_max)) {
      entry->selected = false;
    }

    // check temperature
    if (entry->tmp_min != 0 && (!ok || tmp < entry->tmp_min)) {
      entry->selected = false;
    } else if (entry->tmp_max != 0 && (!ok || tmp > entry->tmp_max)) {
      entry->selected = false;
    }

    // check humidity
    if (entry->hum_min != 0 && (!ok || hum < entry->hum_min)) {
      entry->selected = false;
    } else if (entry->hum_max != 0 && (!ok || hum > entry->hum_max)) {
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
