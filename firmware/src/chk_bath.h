#ifndef CHK_BATH_H
#define CHK_BATH_H

#include "chk.h"

// The bathroom humidity check times how fast airing takes the moisture a
// shower leaves back out: the baseline humidity, the peak the shower reaches,
// and the decay of the excess over the baseline once the window is open,
// fitted exactly as the ventilation check fits a CO2 decay.
//
// How long moisture lingers is what decides whether a bathroom is prone to
// mould, but the device measures conditions, not mould: there is no WHO limit
// for room humidity to hold a bathroom to, and nothing here is a finding about
// anyone's home or habits.

// Where the evaluator leaves its outputs in chk_t.result. The half-life is
// not here: it follows from the rate, and the view derives it.
enum {
  CHK_BATH_K,        // decay rate of the excess humidity, per hour
  CHK_BATH_K_BAND,   // standard error of the rate
  CHK_BATH_R2,       // coefficient of determination of the fit
  CHK_BATH_RH0,      // baseline humidity before the shower (%)
  CHK_BATH_PEAK,     // the highest humidity seen (%)
  CHK_BATH_RH1,      // humidity when the recovery ended (%)
  CHK_BATH_T0,       // room temperature at the baseline (C)
  CHK_BATH_T1,       // room temperature at the end (C)
  CHK_BATH_OUT_HOW,  // where the outdoor air came from, a chk_bath_out_how_t
  CHK_BATH_TOUT,     // outdoor temperature (C), 0 when none was measured
  CHK_BATH_RHOUT,    // outdoor humidity (%), 0 when none was measured
  CHK_BATH_OUT_AGE,  // hours between measuring outside and the check
};

// The phase boundaries the flow marks. The stretches between them are the
// user starting the shower, ending it and opening the window.
enum {
  CHK_BATH_MARK_SETTLED,      // the baseline was taken
  CHK_BATH_MARK_SHOWER_ON,    // the water is running
  CHK_BATH_MARK_SHOWER_DONE,  // the shower is over
  CHK_BATH_MARK_OPENED,       // the window is open: the recovery starts
  CHK_BATH_MARK_STOPPED,      // the recovery run ended
  CHK_BATH_MARKS,
};

// Whether outdoor air was measured at all. It rides in the payload because
// what counts as a fast recovery depends on what the outside air could take,
// and the page says which it was.
typedef enum {
  CHK_BATH_OUT_SKIPPED = 0,
  CHK_BATH_OUT_MEASURED = 1,
} chk_bath_out_how_t;

// How long a measured outdoor reading is remembered: three hours, since the
// weather moves and a morning value says little about an evening shower.
#define CHK_BATH_OUTDOOR_TTL_MS (3LL * 60 * 60 * 1000)

// Samples this close to the baseline carry too little signal for a log fit,
// as with the ventilation decay, and the sensor's own noise is of that order.
#define CHK_BATH_LOG_MARGIN 1.0f

// How many points the fit needs before its slope means anything.
#define CHK_BATH_FIT_MIN 12

// Quality gate, the ventilation check's shape in humidity units: below either,
// the check reports a direction rather than a number.
#define CHK_BATH_GATE_R2 0.8f
#define CHK_BATH_GATE_DROP 2.0f

// The share of the shower's excess that has to be gone before the recovery
// has been watched long enough to stand.
#define CHK_BATH_RECOVERY_SHARE 0.8f

// The verdict scale, on the half-life of the excess in minutes. A design
// choice: no source gives a cut-off, and these are what a bathroom that dries
// within one shower's gap does against one that is still damp an hour later.
#define CHK_BATH_TIER_GOOD 10.0f
#define CHK_BATH_TIER_FAIR 20.0f

// Whether the fit earned a number, and what to say when it did not.
typedef enum {
  CHK_BATH_SOLID,  // the fit stands, the result is a rate
  CHK_BATH_QUICK,  // no usable fit, but the moisture left fast
  CHK_BATH_SLOW,   // no usable fit, and it barely moved
} chk_bath_quality_t;

// Whether the bathroom aired well, for a three-step scale.
typedef enum {
  CHK_BATH_TIER_LOW,
  CHK_BATH_TIER_MID,
  CHK_BATH_TIER_HIGH,
} chk_bath_tier_t;

// Tracks the peak, which the shower run does and the recovery keeps doing:
// humidity often climbs for a minute or two after the water stops.
void chk_bath_peak(chk_t *c, float rh);

// Adds one recovery sample to the fit, given the humidity and the time since
// the window opened. Samples too close to the baseline are ignored rather
// than distorting the fit. Returns true when it contributed.
bool chk_bath_observe(chk_t *c, float rh, int32_t t_ms);

// Solves the fit and fills the result fields. Returns whether the fit earned
// a number; when it did not, the direction the humidity moved is reported.
chk_bath_quality_t chk_bath_evaluate(chk_t *c, int32_t elapsed_ms);

// How much longer the recovery needs to shed `share` of the excess the shower
// left, in ms, or -1 while the fit cannot yet say. Read exactly as the
// ventilation check's own estimate is.
int32_t chk_bath_remaining(chk_t *c, float share);

// The three-step scale for a half-life in minutes, which a rate that did not
// solve reports as -1 and which lands in the lowest step.
chk_bath_tier_t chk_bath_tier(float half_life);

// The remembered outdoor air, one per device as the ventilation check's floor
// is, and living beside it in RTC memory: it survives the deep sleep between
// checks and is lost on a crash, a power-off or a reflash.
void chk_bath_outdoor_set(float tmp, float rh, int64_t now);
void chk_bath_outdoor_forget(void);

// What the check would use at `now`: true with the reading and its age in
// hours while it is under three hours old, false when there is nothing to
// offer and the step has only measuring or skipping to show.
bool chk_bath_outdoor_get(int64_t now, float *tmp, float *rh, float *age_hours);

// The median of the last n readings of a field, which is steadier than a mean
// when one or two come back wrong.
float chk_bath_median(al_sample_field_t field, int n);

#endif  // CHK_BATH_H
