#ifndef CHK_VENT_H
#define CHK_VENT_H

#include "chk.h"

// The ventilation check measures the airing capacity of an open window: how
// fast an open window replaces the air, from the CO2 decay after opening it.
//
// It is NOT the room's everyday ventilation rate. WHO's 0.5 per hour health
// floor and the advice to crack a window for sleeping belong to the
// closed-window rate, which this check does not measure and must not claim.

// Where the evaluator leaves its outputs in chk_t.result. The half-life and
// the time to fresh are not here: both follow from the rate, and the view
// derives them, so the block holds only what cannot be recomputed.
enum {
  CHK_VENT_COUT,      // outdoor CO2, the floor the decay falls towards (ppm)
  CHK_VENT_C0,        // baseline concentration (ppm)
  CHK_VENT_CLAST,     // last concentration seen (ppm)
  CHK_VENT_ACH,       // air changes per hour
  CHK_VENT_ACH_BAND,  // standard error of the rate
  CHK_VENT_R2,        // coefficient of determination of the fit
  CHK_VENT_COUT_HOW,  // where the floor came from, a chk_vent_cout_how_t
  CHK_VENT_COUT_AGE,  // hours between measuring the floor and starting the check
};

// Where the outdoor floor came from. It is carried in the payload, since the
// estimate's accuracy rests on it and the page says which it was.
// The phase boundaries the flow marks, as slots in the context's marks. The
// series starts with the baseline screen; the stretches no measurement owns,
// between the baseline holding and the window confirmed open and after the
// decay stopped, are the user's time at the device, which the page shows as
// such.
enum {
  CHK_VENT_MARK_SETTLED,  // the baseline held and was taken
  CHK_VENT_MARK_OPENED,   // the window confirmed open: the decay starts
  CHK_VENT_MARK_STOPPED,  // the decay run ended
  CHK_VENT_MARKS,
};

typedef enum {
  CHK_VENT_COUT_ASSUMED,   // nothing measured: the sensor's own reference
  CHK_VENT_COUT_MEASURED,  // measured outside, and the reading had settled
  CHK_VENT_COUT_ROUGH,     // measured outside, but still drifting at the cap
} chk_vent_cout_how_t;

// The floor with nothing measured. The SCD41's automatic self-calibration is
// at its factory setting, on, and it adjusts the sensor so that the cleanest
// air of the past week reads 400 ppm: outdoor air reads about 400 in the
// sensor's own units whatever it really is, and the fit needs the floor in
// those units. So 400 is not a guess at outdoor air but the reference the
// calibration built in, which is what makes it honest to assume.
#define CHK_VENT_OUTDOOR_DEFAULT 400.0f

// How long a measured floor is remembered: a week, which is the calibration's
// own period. Within it the sensor's units are the ones the value was measured
// in; after it the calibration may have moved and the reference is the honest
// floor again.
#define CHK_VENT_OUTDOOR_TTL_MS (7LL * 24 * 60 * 60 * 1000)

// A measured floor above this reads like indoor air, which is what a device
// that never left the room settles on. It is put as a question rather than
// refused: urban outdoor air reaches 600 ppm (ASHRAE 2025), so a lower cut
// would reject real readings where measuring outside matters most, and a
// refusal would leave someone on a busy street with no way to finish.
#define CHK_VENT_OUTDOOR_MAX 700.0f

// Samples at or below this far above the floor carry too little signal to
// contribute to a log fit (ASTM E741 decay via Persily 1997).
#define CHK_VENT_LOG_MARGIN 10.0f

// How many points the log fit needs before its slope means anything. The
// same bar for the evaluation and for the estimate the screen shows while
// the decay is still running.
#define CHK_VENT_FIT_MIN 12

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
  CHK_VENT_SOLID,  // the fit stands, the result is a rate
  CHK_VENT_QUICK,  // no usable fit, but the air moved fast
  CHK_VENT_SLOW,   // no usable fit, and the air barely moved
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

// How much longer the decay needs to shed `share` of the excess it started
// with, in ms, or -1 while the fit cannot yet say. The run ends on that share,
// so this is what the measuring screen counts down.
//
// It reads the fit as it stands rather than the gated result: the quality
// gate decides whether a rate may be reported, which is a question about the
// verdict, not about a rough time to go. An early fit can be wide of the
// mark, and the caller is expected to hold the answer to the run's own
// limits, within which being wide of the mark costs little.
int32_t chk_vent_remaining(chk_t *c, float share);

// The remembered outdoor floor, one per device since outdoor air is the same
// for every room of a flat. It lives in RTC memory: it survives the deep sleep
// between checks and is lost on a crash, a power-off or a reflash, after which
// the reference is assumed again. Whether it should be a device parameter
// instead is an open question in the space.

// Remembers a floor measured outside at `now`.
void chk_vent_outdoor_set(float ppm, bool rough, int64_t now);

// Forgets it, so the reference is assumed again.
void chk_vent_outdoor_forget(void);

// What the check would use at `now`: the remembered floor while it is under a
// week old, the reference otherwise. Reports where it came from, and its age
// in hours, which is zero for the reference.
chk_vent_cout_how_t chk_vent_outdoor_get(int64_t now, float *ppm, float *age_hours);

// The median of the last n readings, which is steadier than a mean when one
// or two come back wrong.
float chk_vent_baseline_median(int n);

// The three-step scale for a solved rate.
chk_vent_tier_t chk_vent_tier(float ach);

#endif  // CHK_VENT_H
