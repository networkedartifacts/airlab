#ifndef CHK_BEDROOM_H
#define CHK_BEDROOM_H

#include "chk.h"

// The bedroom night check records one night in a bedroom set up a given way:
// how high the CO2 climbs behind a closed or tilted window, how long it stays
// there, and how much outdoor air each sleeper is getting by morning.
//
// It is one night, not two compared: comparing setups is the page's work, not
// the device's. The CO2 levels it reports are the findings of the sleep
// literature, not limits: no source gives a limit for a bedroom, and no
// sleep improvement is promised to anyone from a number on this screen.

// Where the evaluator leaves its outputs in chk_t.result. The last slot is
// the evaluator's own bookkeeping rather than a result, and the view leaves
// it out.
enum {
  CHK_BEDROOM_C0,         // baseline before lying down (ppm)
  CHK_BEDROOM_CMAX,       // the highest CO2 of the night (ppm)
  CHK_BEDROOM_CMEAN,      // the night's mean (ppm)
  CHK_BEDROOM_C1,         // the last reading, which is waking (ppm)
  CHK_BEDROOM_OVER_LOW,   // hours spent above CHK_BEDROOM_LEVEL_LOW
  CHK_BEDROOM_OVER_HIGH,  // hours spent above CHK_BEDROOM_LEVEL_HIGH
  CHK_BEDROOM_TMIN,       // the band the temperature stayed in (C)
  CHK_BEDROOM_TMAX,
  CHK_BEDROOM_RHMIN,  // and the humidity (%)
  CHK_BEDROOM_RHMAX,
  CHK_BEDROOM_SLEEPERS,  // how many slept here, 1 to 3, 0 when not said
  CHK_BEDROOM_SETUP,     // how the room was set up, a chk_bedroom_setup_t
  CHK_BEDROOM_FLOW,      // outdoor air per sleeper from the last hour (l/s)
  CHK_BEDROOM_PLATEAU,   // 1 when the last hour held, so the flow is an estimate rather than a floor
  CHK_BEDROOM_LAST_S,    // seconds into the night of the last reading folded in, -1 before the first
};

// The phase boundaries the flow marks, as slots in the context's marks. The
// stretches between them, the prompt before lying down and the screens after
// waking, are the user's time at the device.
enum {
  CHK_BEDROOM_MARK_SETTLED,  // the baseline held and was taken
  CHK_BEDROOM_MARK_ASLEEP,   // the sleepers lay down: the night starts here
  CHK_BEDROOM_MARK_WOKE,     // the key came in the morning
  CHK_BEDROOM_MARKS,
};

// The accumulator slots. A context cannot hold a sample stream, so the last
// hour is kept as two buckets rather than as a window: the hour being filled,
// and the one before it for while that is still short.
enum {
  CHK_BEDROOM_FIT_NIGHT,  // the whole night, for its mean
  CHK_BEDROOM_FIT_HOUR,   // the hour being filled
  CHK_BEDROOM_FIT_PRIOR,  // the one before it
};

// How the room was set up for the night, in the order the picker lists them.
typedef enum {
  CHK_BEDROOM_SETUP_NONE = 0,
  CHK_BEDROOM_SETUP_CLOSED = 1,
  CHK_BEDROOM_SETUP_TILTED = 2,
  CHK_BEDROOM_SETUP_OPEN = 3,
  CHK_BEDROOM_SETUP_DOOR = 4,
} chk_bedroom_setup_t;

// The two levels the night's hours are counted against. They are the findings
// of the ASHRAE sleep brief: above 1150 ppm the sleep measures it collects
// worsen, and 2600 is where the sharper effects sit. Neither is a limit
// anyone publishes.
#define CHK_BEDROOM_LEVEL_LOW 1150.0f
#define CHK_BEDROOM_LEVEL_HIGH 2600.0f

// The CO2 a sleeping adult gives off: 0.0035 litres a second at 1.0 met
// (Persily and de Jonge 2017). At a steady state the outdoor air each sleeper
// gets is G / (C - Cout), which with the difference in ppm is
// 3500 / (C - 400) litres a second, whatever the number of sleepers: each
// one brings their own generation with them.
#define CHK_BEDROOM_GEN_LPS 0.0035f

// The floor the balance is taken against, which is the sensor's own reference
// rather than a claim about outdoor air, exactly as in the ventilation check.
#define CHK_BEDROOM_OUTDOOR 400.0f

// A night shorter than this has not settled anywhere, and no flow is reported
// from it.
#define CHK_BEDROOM_FLOW_MIN_MS (2 * 3600 * 1000)

// The last hour held when its CO2 moved less than this across it: a design
// choice. Under it the room reached a balance and the flow is an estimate of
// it; over it the room was still filling and the flow is a lower bound.
#define CHK_BEDROOM_PLATEAU_PPM 100.0f

// The points the last hour's fit needs before its slope means anything, and
// the half hour of it that must be filled before the bucket stands on its own.
#define CHK_BEDROOM_FIT_MIN 12
#define CHK_BEDROOM_HOUR_MIN_MS 1800000

// A gap longer than this is the device asleep or the sensor out rather than
// time the check watched, so it counts towards no level.
#define CHK_BEDROOM_GAP_MAX_MS 300000

// The verdict scale, on the night's mean. The sleep studies state their bands
// on the mean of the night rather than on its peak, with effects already
// showing between 800 and 1000 ppm, so the scale steps where they do: the
// steps themselves are this branch's design choice.
#define CHK_BEDROOM_TIER_GOOD 800.0f
#define CHK_BEDROOM_TIER_FAIR 1150.0f

// Whether the night stayed fresh, for a three-step scale.
typedef enum {
  CHK_BEDROOM_TIER_LOW,
  CHK_BEDROOM_TIER_MID,
  CHK_BEDROOM_TIER_HIGH,
} chk_bedroom_tier_t;

// Clears the night's statistics, so the run that follows starts from nothing.
// What the user entered before it, the baseline and the two pickers, is kept.
void chk_bedroom_reset(chk_t *c);

// Folds one reading of the night into the statistics: the CO2 and, beside it,
// the temperature and humidity the same sample carried, either of which may
// be NaN when the sensor had none.
void chk_bedroom_observe(chk_t *c, float co2, float tmp, float rh, int32_t t_ms);

// The outdoor air one sleeper is getting at this concentration, in litres a
// second, or 0 for a concentration at or below the reference.
float chk_bedroom_flow(float co2);

// Solves the night: its mean, the flow the last hour implies and whether that
// hour held. Everything else is folded in as it goes.
void chk_bedroom_evaluate(chk_t *c, int32_t elapsed_ms);

// The three-step scale for a night, on the mean it held. The peak is a
// statistic beside it, not what the verdict is graded on.
chk_bedroom_tier_t chk_bedroom_tier(float cmean);

#endif  // CHK_BEDROOM_H
