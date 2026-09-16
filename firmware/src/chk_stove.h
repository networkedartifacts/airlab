#ifndef CHK_STOVE_H
#define CHK_STOVE_H

#include "chk.h"

// The gas stove check estimates how much of a burner's exhaust the extractor
// hood catches, using CO2 as a combustion tracer.
//
// It uses CO2 because the device has neither an NO2 nor a CO sensor, and the
// NOx index is relative to a learned baseline. A 2 kW flame emits about three
// litres of CO2 a minute, ten times a person, which makes it a strong tracer
// over a short pass. The check therefore says nothing about NO2, nothing about
// carbon monoxide, and nothing about whether a burner is faulty.
//
// Three passes with a pot of water (Singer 2016): burner on with the hood off,
// the hood clearing, then burner on with the hood on. The ratio of the two
// rise rates is the capture efficiency; the clearing decay is the hood's own
// air change rate.

// Where the evaluator leaves its outputs in chk_t.result.
enum {
  CHK_STOVE_C0,        // baseline before any burner (ppm)
  CHK_STOVE_SLOPE1,    // rise with the hood off (ppm/min)
  CHK_STOVE_SLOPE3,    // rise with the hood on (ppm/min)
  CHK_STOVE_CAPTURE,   // share of the exhaust the hood catches, 0 to 1
  CHK_STOVE_HOOD_ACH,  // the hood's air changes per hour, from the clearing
  CHK_STOVE_PEAK,      // the highest CO2 the check saw (ppm)
  CHK_STOVE_NOX,       // highest NOx index seen, a qualitative companion
  CHK_STOVE_R2,        // the weaker of the two rise fits
};

// The passes, in order, which are also the accumulator slots.
enum {
  CHK_STOVE_PASS_OPEN,   // burner on, hood off
  CHK_STOVE_PASS_CLEAR,  // burner off, hood on
  CHK_STOVE_PASS_HOOD,   // burner on, hood on
};

// The slope is taken over the first two minutes of a pass, before the excess
// that has built up starts feeding the hood's own removal and bends the rise.
#define CHK_STOVE_SLOPE_MS 120000

// A pass runs at most five minutes, and a burn pass may stop early once it has
// raised the room by this much, which is plenty of signal for a slope.
#define CHK_STOVE_PASS_MAX_MS 300000
#define CHK_STOVE_BURN_RISE 500.0f

// Where the experiment stops rather than where health begins: this is the
// occupational eight-hour limit, used here only as the point at which a
// kitchen filling with combustion products should be aired rather than
// measured further. The check makes no health claim from it.
#define CHK_STOVE_ABORT_PPM 5000.0f

// A rise fit has to be this convincing before a ratio of two of them means
// anything. A design choice, as no source gives a cut-off.
#define CHK_STOVE_GATE_R2 0.8f

// Benchmarks from LBNL 2020: 70 per cent keeps the 1-hour NO2 guideline in
// over 99 per cent of homes and 60 per cent does it for PM2.5. Installed
// hoods average about 55 per cent.
#define CHK_STOVE_TIER_GOOD 0.70f
#define CHK_STOVE_TIER_FAIR 0.55f

typedef enum {
  CHK_STOVE_SOLID,  // both rises fit, the ratio stands
  CHK_STOVE_UNCLEAR,
} chk_stove_quality_t;

typedef enum {
  CHK_STOVE_TIER_LOW,
  CHK_STOVE_TIER_MID,
  CHK_STOVE_TIER_HIGH,
} chk_stove_tier_t;

// Adds a reading from a burn pass, which is fitted as a straight rise in ppm
// against minutes. Readings past the slope window are ignored, and the pass
// is still watched for the abort level. Returns true when it contributed.
bool chk_stove_observe_burn(chk_t *c, int pass, float co2, int32_t t_ms);

// Adds a reading from the clearing pass, fitted as a decay towards the
// baseline exactly as the ventilation check fits one.
bool chk_stove_observe_clear(chk_t *c, float co2, int32_t t_ms);

// Solves both rises and the clearing decay, and fills the result fields.
chk_stove_quality_t chk_stove_evaluate(chk_t *c);

// The three-step scale for a solved capture efficiency.
chk_stove_tier_t chk_stove_tier(float capture);

#endif  // CHK_STOVE_H
