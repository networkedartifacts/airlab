// What a finished check looks like, built from its result block alone.
//
// This exists so that reopening a stored check needs nothing but its header:
// the same function serves the live flow and the stored one, which is what
// keeps the two from drifting apart. Note the ceiling: lvx_fmt rotates eight
// buffers, so a view may not hold more strings than that at once.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "chk.h"
#include "chk_stove.h"
#include "chk_vent.h"
#include "lvx.h"

// how many samples fall between two phase boundaries, at this cadence
static float chk_view_span(int32_t from_ms, int32_t to_ms, uint8_t cadence) {
  if (cadence == 0 || to_ms <= from_ms) {
    return 0;
  }
  return (float)((to_ms - from_ms) / 1000 / cadence);
}

static void chk_view_vent(const float *r, const int32_t *marks, uint8_t cadence, chk_view_t *v) {
  v->title = CHK_TEXT(vent__title);
  v->letter = 'A';
  v->signal = AL_SAMPLE_CO2;
  v->note = CHK_TEXT(vent__stat_note);

  int half = chk_round_minutes(r[CHK_VENT_HALF_LIFE]);
  v->lines[0] = lvx_fmt(CHK_TEXT(vent__stat_ach), r[CHK_VENT_ACH], r[CHK_VENT_ACH_BAND]);
  v->lines[1] = lvx_fmt(CHK_TEXT(vent__stat_half_life), half);
  v->lines[2] = lvx_fmt(CHK_TEXT(vent__stat_fresh), chk_round_minutes(r[CHK_VENT_FRESH]));
  v->lines[3] = lvx_fmt(CHK_TEXT(vent__stat_co2), r[CHK_VENT_C0], r[CHK_VENT_CLAST], r[CHK_VENT_COUT]);
  v->num_lines = 4;

  // ach, achSe, r2, c0, c1, cout, pre
  v->payload[0] = r[CHK_VENT_ACH];
  v->payload[1] = r[CHK_VENT_ACH_BAND];
  v->payload[2] = r[CHK_VENT_R2];
  v->payload[3] = r[CHK_VENT_C0];
  v->payload[4] = r[CHK_VENT_CLAST];
  v->payload[5] = r[CHK_VENT_COUT];
  // the baseline samples at the head of the series, which is where the page
  // stops shading the chart as "before the window opened"
  v->payload[6] = chk_view_span(0, marks[0], cadence);
  v->num_payload = 7;

  // the one number the verdict is about
  v->headline = lvx_fmt(CHK_TEXT(vent__verdict_half_life), half);
}

static void chk_view_stove(const float *r, const int32_t *marks, uint8_t cadence, chk_view_t *v) {
  v->title = CHK_TEXT(stove__title);
  v->letter = 'E';
  v->signal = AL_SAMPLE_CO2;
  v->note = CHK_TEXT(stove__stat_note);

  int percent = (int)(r[CHK_STOVE_CAPTURE] * 100 + 0.5f);
  v->lines[0] = lvx_fmt(CHK_TEXT(stove__stat_capture), percent);
  v->lines[1] = r[CHK_STOVE_HOOD_ACH] > 0 ? lvx_fmt(CHK_TEXT(stove__stat_hood), r[CHK_STOVE_HOOD_ACH])
                                          : CHK_TEXT(stove__stat_hood_none);
  v->lines[2] = lvx_fmt(CHK_TEXT(stove__stat_peak), r[CHK_STOVE_PEAK]);
  v->num_lines = 3;

  // ce, hoodAch, slope1, slope3, c0, noxPeak, pre, pass1, pass2, pass3
  v->payload[0] = (float)percent;
  v->payload[1] = r[CHK_STOVE_HOOD_ACH] > 0 ? r[CHK_STOVE_HOOD_ACH] : 0;
  v->payload[2] = r[CHK_STOVE_SLOPE1];
  v->payload[3] = r[CHK_STOVE_SLOPE3];
  v->payload[4] = r[CHK_STOVE_C0];
  v->payload[5] = r[CHK_STOVE_NOX];
  // the baseline, then a count per pass, from the boundaries the flow marked
  v->payload[6] = chk_view_span(0, marks[0], cadence);
  v->payload[7] = chk_view_span(marks[0], marks[1], cadence);
  v->payload[8] = chk_view_span(marks[1], marks[2], cadence);
  v->payload[9] = chk_view_span(marks[2], marks[3], cadence);
  v->num_payload = 10;

  v->headline = lvx_fmt(CHK_TEXT(stove__verdict), percent);
}

bool chk_describe(uint8_t id, const float *result, const int32_t *marks, uint8_t marks_len, uint8_t cadence,
                  chk_view_t *out) {
  if (result == NULL || out == NULL || marks == NULL || marks_len < 4) {
    return false;
  }

  memset(out, 0, sizeof(*out));

  switch (id) {
    case CHK_VENT:
      chk_view_vent(result, marks, cadence, out);
      return true;
    case CHK_STOVE:
      chk_view_stove(result, marks, cadence, out);
      return true;
    default:
      return false;
  }
}
