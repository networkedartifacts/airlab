// Check copy, shared between the firmware (chk.c) and the host tests (test/).
//
// This is deliberately separate from scr_trans.inc. That table is complete in
// every language and test/scr.c enforces it: a string set in one language and
// left NULL in another fails the build. The guided checks ship English-only
// for the first trial, so they get their own table with a weaker rule —
// English is required, the other languages may be NULL, and chk_text() falls
// back to English when one is. test/chk.c enforces that weaker rule, so the
// exemption is visible in the code rather than implied by an absence.
//
// The language order matches scr_lang_t, which test/chk.c asserts, so that
// chk_init() can simply be handed scr_lang(). The enum is repeated here
// rather than included so that chk.c does not pull scr_trans.inc's tables
// into a second translation unit.

typedef enum {
  CHK_DE,
  CHK_EN,
  CHK_ES,
  CHK_FR,
} chk_lang_t;

typedef struct {
  // shared across checks
  const char* next;
  const char* start;
  const char* ok;
  const char* again;
  const char* done;
  const char* check;
  const char* back;
  const char* sensor_errors;
  const char* stage__baseline;
  const char* stage__measuring;
  const char* stage__results;

  // ventilation
  const char* vent__title;
  const char* vent__intro_1;
  const char* vent__intro_2;
  const char* vent__intro_3;
  const char* vent__intro_4;
  const char* vent__list_windows;
  const char* vent__list_door;
  const char* vent__list_table;
  const char* vent__already_fresh;
  const char* vent__baseline_hint;
  const char* vent__open_window;
  const char* vent__window_is_open;
  const char* vent__unclear;
  const char* vent__verdict_half_life;
  const char* vent__verdict_second_window;
  const char* vent__stat_ach;
  const char* vent__stat_half_life;
  const char* vent__stat_co2;
  const char* vent__stat_note;
} chk_trans_t;

static const chk_trans_t chk_trans_map[] = {
    [CHK_EN] =
        {
            .next = "Next",
            .start = "Start",
            .ok = "OK",
            .again = "Again",
            .done = "Done",
            .check = "Check",
            .back = "Back",
            .sensor_errors = "Too many sensor errors. Try again.",
            .stage__baseline = "Baseline",
            .stage__measuring = "Measuring",
            .stage__results = "Results",

            .vent__title = "Air Check: Ventilation",
            .vent__intro_1 = "Glad to assist with a ventilation check!",
            .vent__intro_2 = "First a baseline, with windows and doors closed.",
            .vent__intro_3 = "Then open a window wide and step out.",
            .vent__intro_4 = "We time how fast the stale air leaves.",
            .vent__list_windows = "Windows closed",
            .vent__list_door = "Door closed",
            .vent__list_table = "Device on a table",
            .vent__already_fresh = "Already fresh! Close the door, stay 20 min, then retry.",
            .vent__baseline_hint = "Keep windows closed",
            .vent__open_window = "Got it: %.0f ppm. Open a window wide, then step out!",
            .vent__window_is_open = "Window is open",
            .vent__unclear = "Hard to pin down a number, but this room ventilates %s.",
            .vent__verdict_half_life = "Airing clears half the stale air in %d min.",
            .vent__verdict_second_window = "A second window would speed it up.",
            .vent__stat_ach = "ACH: %.1f +/- %.1f",
            .vent__stat_half_life = "Half-life: %d min",
            .vent__stat_co2 = "CO2: %.0f to %.0f (out %.0f)",
            .vent__stat_note = "ACH = air changes per hour",
        },
};
