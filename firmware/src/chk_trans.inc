// The check copy itself. The types live in chk.h; this file is the data and
// is included by chk.c alone.

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
