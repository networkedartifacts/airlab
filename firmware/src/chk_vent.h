#ifndef CHK_VENT_H
#define CHK_VENT_H

#include "chk.h"

// The ventilation check measures the airing capacity of an open window: how
// fast an open window replaces the air, from the CO2 decay after opening it.
//
// It is NOT the room's everyday ventilation rate. WHO's 0.5 per hour health
// floor and the advice to crack a window for sleeping belong to the
// closed-window rate, which this check does not measure and must not claim.

// Where the evaluator leaves its outputs in chk_t.result.
enum {
  CHK_VENT_COUT,       // outdoor CO2, the floor the decay falls towards (ppm)
  CHK_VENT_C0,         // baseline concentration (ppm)
  CHK_VENT_CLAST,      // last concentration seen (ppm)
  CHK_VENT_ACH,        // air changes per hour
  CHK_VENT_ACH_BAND,   // standard error of the rate
  CHK_VENT_R2,         // coefficient of determination of the fit
  CHK_VENT_HALF_LIFE,  // minutes to clear half the stale air
  CHK_VENT_FRESH,      // minutes to 95 per cent fresh
};

// Samples at or below this far above the floor carry too little signal to
// contribute to a log fit (ASTM E741 decay via Persily 1997).
#define CHK_VENT_LOG_MARGIN 10.0f

// Quality gate. Both are the branch's own values, kept: below either, the
// check reports a direction rather than a number.
#define CHK_VENT_GATE_R2 0.8f
#define CHK_VENT_GATE_DROP 40.0f

// The verdict scale. A design choice, not a threshold from any source: six
// air changes an hour is brisk airing, two is BAG's cross-draught advice,
// below that a tilted window is not airing at all.
#define CHK_VENT_TIER_GOOD 6.0f
#define CHK_VENT_TIER_FAIR 2.0f

// Whether the fit earned a number, and what to say when it did not.
typedef enum {
  CHK_VENT_SOLID,    // the fit stands, the result is a rate
  CHK_VENT_QUICK,    // no usable fit, but the air moved fast
  CHK_VENT_SLOW,     // no usable fit, and the air barely moved
} chk_vent_quality_t;

// Whether the room aired well, for a three-step scale.
typedef enum {
  CHK_VENT_TIER_LOW,
  CHK_VENT_TIER_MID,
  CHK_VENT_TIER_HIGH,
} chk_vent_tier_t;

// Adds one decay sample to the fit, given the concentration and the time
// since the window opened. Samples too close to the outdoor floor are
// ignored rather than distorting the fit. Returns true when it contributed.
bool chk_vent_observe(chk_t *c, float co2, int32_t t_ms);

// Solves the fit and fills the result fields. Returns whether the fit earned
// a number; when it did not, the drop rate decides which direction to report.
chk_vent_quality_t chk_vent_evaluate(chk_t *c, int32_t elapsed_ms);

// Guesses outdoor CO2 from the lowest reading the device has lately seen,
// floored at 400 ppm. This is a guess and the user may overrule it.
//
// NOTE: its worth depends on whether the sensor's automatic self-calibration
// is enabled, since that recalibrates so the rolling minimum reads about
// 400 ppm — which would make this number an artefact of the calibration
// rather than a measurement of outdoor air. Open question.
float chk_vent_outdoor_guess(void);

// The median of the last n readings, which is steadier than a mean when one
// or two come back wrong.
float chk_vent_baseline_median(int n);

// The three-step scale for a solved rate.
chk_vent_tier_t chk_vent_tier(float ach);

#endif  // CHK_VENT_H
