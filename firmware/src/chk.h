#ifndef CHK_H
#define CHK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>

#include <al/sample.h>

#include "chk_code.h"

// The guided air checks. A check is a screen function that calls the kit
// below and returns an outcome; control flow stays ordinary C, because that
// is where the per-check variety lives.

#define CHK_MARKS 8     // phase boundaries per check
#define CHK_PASSES 3    // fitted segments per check (the stove check uses all)
#define CHK_RESULTS 16  // evaluator outputs per check, with room to grow: both checks filled eight

// What a kit call reports back. The caller maps these to screens; only
// CHK_NEXT continues the flow.
typedef enum {
  CHK_NEXT,   // carry on
  CHK_BACK,   // user went back a screen
  CHK_AGAIN,  // restart this check
  CHK_EXIT,   // user left
  CHK_IDLE,   // timed out
} chk_result_t;

// What a check makes of one sample.
typedef enum {
  CHK_STEP_WAIT,  // taken, but nothing is moving yet
  CHK_STEP_GO,    // taken, and the signal is moving
  CHK_STEP_DONE,  // the check has what it needs
} chk_step_t;

typedef struct {
  int32_t min_ms;    // the earliest a check may declare itself done
  int32_t max_ms;    // the latest the run may go on, 0 for no limit
  int capacity;      // samples the run may take, 0 for no limit
  int32_t nudge_ms;  // how long to wait before prompting, 0 to never
  bool settle;       // the engine ends the run once the reading has settled
} chk_measure_cfg_t;

// The settle classifier: a run that waits for the sensor to catch up with the
// air rather than for a count. Readings are taken in windows of six, half a
// minute at the trial cadence, and each window is reduced to its median so one
// wild reading neither breaks nor fakes the verdict. Settled means the newest
// window's median is within ten ppm of the one before it: the samples have held
// within that band for a whole minute. The engine's floor and cap frame it, as
// a step of a few hundred ppm still moves more than that per window until
// about two minutes in, and a reading still drifting at the cap is taken as it
// stands. The band is the fit's own margin above the floor. The sources give
// no settling rule, only the numbers that constrain one: the sensor's
// minute-long time constant, its ten ppm of noise, and the four ppm a person
// adds to a small room per half minute, which the band has to tolerate since a
// baseline with someone in the room never reaches a plateau.
#define CHK_SETTLE_WINDOW 6
#define CHK_SETTLE_BAND 10.0f

// The sensor's time constant, from the datasheet: it reaches 63 per cent of a
// step in about a minute. The estimate of the time left rests on it.
#define CHK_SETTLE_TAU_MS 60000

typedef struct {
  float window[CHK_SETTLE_WINDOW];  // the window being filled
  uint8_t fill;                     // readings in it
  float last;                       // median of the newest complete window, NaN before one
  float prior;                      // median of the one before, NaN before two
  int32_t eta;                      // ms since the run began at which it should settle, -1 unknown
  bool settled;                     // the last two windows agreed
} chk_settle_t;

typedef struct {
  int64_t began;        // epoch ms the run started, 0 until it has been entered
  int attempts;         // readings asked for
  int fails;            // readings that came back unusable
  int count;            // readings that counted
  int32_t elapsed;      // ms since the run began
  bool nudging;         // the user should be prompted
  chk_settle_t settle;  // the classifier's state, for a run that settles
} chk_measure_run_t;

// Least-squares terms accumulated one sample at a time. The context must fit
// in RTC-retained memory so a check survives deep sleep, which is why a check
// never holds its sample stream: the stream lives in the device's own stores
// and the fit is computed from these running sums.
typedef struct {
  int n;
  double sx;
  double sy;
  double sxx;
  double sxy;
  double syy;
} chk_accum_t;

// The in-flight state of a check. Retained across deep sleep, lost on a crash.
typedef struct {
  uint8_t id;                // which check, or CHK_NONE when idle
  uint8_t step;              // how far through the flow, for resuming
  uint8_t room;              // where the check runs, a chk_code_room_t, none until the flow says
  int64_t start;             // epoch ms
  uint8_t phase;             // current phase within a step
  int32_t marks[CHK_MARKS];  // phase boundaries, ms since start
  chk_accum_t accum[CHK_PASSES];
  float result[CHK_RESULTS];  // evaluator outputs
  int64_t seen;               // epoch of the last sample folded in
  int64_t kept;               // epoch of the last sample copied onto the record
  chk_measure_run_t run;      // the run in progress
  uint16_t file;              // the record on flash, 0 until one is opened
  uint16_t record;            // seconds between the readings kept on the record, 0 for the device's own cadence
} chk_t;

// No check is in progress. A context holding this is free to be begun.
#define CHK_NONE 0xFF

// The check in progress, which lives in RTC-retained memory so that it
// survives a deep sleep. A deep sleep on this device is a reset: main memory
// is lost, the flow is re-entered from the top, and `step` is what lets it
// pick up where it stopped. Nothing here survives a crash, by decision.
chk_t* chk_context(void);

// True when this context is already part-way through the given check, so the
// flow should resume rather than start over.
bool chk_resuming(const chk_t* c, uint8_t id);

// True once the check has folded in a reading, which is the baseline under
// way. Before that the user is looking; from here on they have started the
// check and are waiting for it, which decides what a prompt left alone does.
bool chk_started(const chk_t* c);

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
  const char* stop;
  const char* discard;
  const char* carry_on;
  const char* sensor_errors;
  const char* no_checks;
  const char* start_check;
  const char* stage__baseline;
  const char* stage__measuring;
  const char* stage__results;
  const char* stage__share;
  const char* stage__outside;
  const char* share_scan;
  const char* share_failed;
  const char* settle_left;
  const char* settle_soon;

  // ventilation
  const char* vent__title;
  const char* vent__name;
  const char* vent__intro_1;
  const char* vent__intro_2;
  const char* vent__intro_3;
  const char* vent__intro_4;
  const char* vent__room_ask;
  const char* vent__list_windows;
  const char* vent__list_door;
  const char* vent__list_table;
  const char* vent__outdoor_ask;
  const char* vent__outdoor_default;
  const char* vent__outdoor_remembered;
  const char* vent__outdoor_measure;
  const char* vent__age_now;
  const char* vent__age_hours;
  const char* vent__age_days;
  const char* vent__go_outside;
  const char* vent__outside_hint;
  const char* vent__outdoor_got;
  const char* vent__outdoor_doubt;
  const char* vent__outdoor_anyway;
  const char* vent__already_fresh;
  const char* vent__nudge;
  const char* vent__quickly;
  const char* vent__slowly;
  const char* vent__advice_low;
  const char* vent__advice_mid;
  const char* vent__advice_high;
  const char* vent__baseline_hint;
  const char* vent__open_window;
  const char* vent__window_is_open;
  const char* vent__unclear;
  const char* vent__verdict_half_life;
  const char* vent__verdict_second_window;
  const char* vent__stat_ach;
  const char* vent__stat_half_life;
  const char* vent__stat_fresh;
  const char* vent__stat_co2;
  const char* vent__stat_note;

  // the room registry, in its order, for the picker
  const char* room__none;
  const char* room__living;
  const char* room__bedroom;
  const char* room__kitchen;
  const char* room__office;
  const char* room__bathroom;
  const char* room__kids;
  const char* room__meeting;
  const char* room__classroom;
  const char* room__hallway;
  const char* room__basement;
  const char* room__workshop;
  const char* room__garage;
  const char* room__car;
  const char* room__hotel;
  const char* room__outdoors;

  // gas stove
  const char* stove__title;
  const char* stove__name;
  const char* stove__intro_1;
  const char* stove__intro_2;
  const char* stove__intro_3;
  const char* stove__list_off;
  const char* stove__list_pot;
  const char* stove__list_away;
  const char* stove__baseline_hint;
  const char* stove__stage_open;
  const char* stove__stage_clear;
  const char* stove__stage_hood;
  const char* stove__pass_open;
  const char* stove__pass_clear;
  const char* stove__pass_hood;
  const char* stove__burner_on;
  const char* stove__hood_on;
  const char* stove__nudge;
  const char* stove__too_much;
  const char* stove__unclear_1;
  const char* stove__unclear_2;
  const char* stove__verdict;
  const char* stove__advice_low;
  const char* stove__advice_mid;
  const char* stove__advice_high;
  const char* stove__stat_capture;
  const char* stove__stat_hood;
  const char* stove__stat_hood_none;
  const char* stove__stat_peak;
  const char* stove__stat_note;

  // bedroom night
  const char* bedroom__title;
  const char* bedroom__name;
  const char* bedroom__intro_1;
  const char* bedroom__intro_2;
  const char* bedroom__intro_3;
  const char* bedroom__intro_4;
  const char* bedroom__setup_ask;
  const char* bedroom__setup_closed;
  const char* bedroom__setup_tilted;
  const char* bedroom__setup_open;
  const char* bedroom__setup_door;
  const char* bedroom__sleepers_ask;
  const char* bedroom__sleepers_one;
  const char* bedroom__sleepers_two;
  const char* bedroom__sleepers_more;
  const char* bedroom__list_window;
  const char* bedroom__list_door;
  const char* bedroom__list_place;
  const char* bedroom__baseline_hint;
  const char* bedroom__lie_down;
  const char* bedroom__lying_down;
  const char* bedroom__stage_night;
  const char* bedroom__good_morning;
  const char* bedroom__verdict;
  const char* bedroom__advice_low;
  const char* bedroom__advice_mid;
  const char* bedroom__advice_high;
  const char* bedroom__stat_peak;
  const char* bedroom__stat_mean;
  const char* bedroom__stat_over;
  const char* bedroom__stat_temp;
  const char* bedroom__stat_hum;
  const char* bedroom__stat_flow;
  const char* bedroom__stat_flow_none;
  const char* bedroom__stat_note;

  // bathroom humidity
  const char* bath__title;
  const char* bath__name;
  const char* bath__intro_1;
  const char* bath__intro_2;
  const char* bath__intro_3;
  const char* bath__outdoor_ask;
  const char* bath__outdoor_remembered;
  const char* bath__outdoor_measure;
  const char* bath__outdoor_skip;
  const char* bath__go_outside;
  const char* bath__outside_hint;
  const char* bath__outdoor_got;
  const char* bath__list_window;
  const char* bath__list_door;
  const char* bath__list_place;
  const char* bath__baseline_hint;
  const char* bath__shower_ask;
  const char* bath__shower_is_on;
  const char* bath__stage_shower;
  const char* bath__shower_done;
  const char* bath__shower_nudge;
  const char* bath__open_window;
  const char* bath__window_is_open;
  const char* bath__stage_airing;
  const char* bath__verdict;
  const char* bath__unclear;
  const char* bath__advice_low;
  const char* bath__advice_mid;
  const char* bath__advice_high;
  const char* bath__stat_baseline;
  const char* bath__stat_peak;
  const char* bath__stat_end;
  const char* bath__stat_half;
  const char* bath__stat_temp;
  const char* bath__stat_out;
  const char* bath__stat_out_none;
  const char* bath__stat_note;
} chk_trans_t;

// The checks themselves.
typedef enum {
  CHK_VENT,
  CHK_STOVE,
  CHK_BEDROOM,
  CHK_BATH,
} chk_id_t;

// How long a prompt waits for its key before deciding nobody is there. A
// check nobody has started gives the device back to the idle screen; one
// that has been started stays on the prompt, sleeping through the wait
// where the cadence allows, until it is dismissed.
#define CHK_ACTION_TIMEOUT 60000

// One thing Robin says, with the sign under the A key.
typedef struct {
  const lv_img_dsc_t* mood;
  const char* text;
  const char* action;
} chk_bubble_t;

// Says a sequence of bubbles, one key press each, and returns once the last
// is acknowledged. The action of each bubble labels the key; a NULL action
// falls back to the shared "Next". The B key steps back through the sequence
// and out of it from the first bubble. A sequence entered backwards starts at
// `start`, its last bubble.
chk_result_t chk_say_from(const chk_bubble_t* bubbles, size_t count, size_t start);
chk_result_t chk_say(const chk_bubble_t* bubbles, size_t count);

// Draws the header every non-dialogue check screen carries. Must be called
// between gfx_begin and gfx_end.
void chk_chrome(const char* title, const char* stage);

// Shows a checklist the user confirms before a check starts, so that the
// single-zone assumption the decay and rise methods rest on is actually met.
// The key ticks the items off one at a time; `done` shows them ticked already,
// for a list come back to.
chk_result_t chk_list(const char* title, const char* stage, const char* const* items, size_t count, bool done);

// Draws the QR code a phone scans to carry the result away, with a short
// line beside it. Returns once acknowledged.
chk_result_t chk_qr(const char* title, const char* digits, const char* caption);

// Shows a run's result as labelled lines with a note beneath.
chk_result_t chk_stats(const char* title, const char* stage, const char* const* lines, size_t count, const char* note);

// The signals a check needs, as a mask over al_sample_field_t. The feature
// runs on both devices and they do not carry the same sensors: PM2.5 arrives
// with the BMV080 on Air Lab 2, so a check that needs it must not be offered
// on a device without one.
#define CHK_NEEDS(field) (1u << (field))
#define CHK_NEEDS_CO2 CHK_NEEDS(AL_SAMPLE_CO2)
#define CHK_NEEDS_PM CHK_NEEDS(AL_SAMPLE_PM)

// Reports whether the device in hand can run a check with these needs.
bool chk_available(uint16_t needs);

// Begins a check: stamps the start time, from which every phase boundary and
// the recorded window are measured.
void chk_begin(chk_t* c, uint8_t id);

// Ends a check, freeing the context for the next one.
void chk_end(chk_t* c);

// Releases the context of a check that is over, finished or abandoned, and
// drops a record it left unfinished. The flows call this rather than chk_end,
// which resets the context alone.
void chk_release(chk_t* c);

// Starts the check over at the step it is on: everything measured so far is
// dropped, the record with it, and the clock starts anew, so a baseline done
// again heads the series as a first one would. What the user entered before
// measuring, the results so far, is kept.
void chk_restart(chk_t* c);

// The room a check last ran in, so the picker opens on it: one per device,
// RTC-retained like the outdoor floor, and forgotten with it on a power-off.
// A device is carried from room to room, so it is a default, not a setting.
uint8_t chk_room_last(void);
void chk_room_remember(uint8_t room);

// True while leaving would throw the check away: it has taken readings and
// no result has been sealed yet.
bool chk_underway(const chk_t* c);

// Asks whether to stop the check, the way a recording asks on the way out.
// Returns true to stop; a timeout keeps going.
bool chk_confirm_stop(void);

// Asks whether to throw the baseline away, which going back past it means.
// Returns true to discard; a timeout keeps going.
bool chk_confirm_discard(void);

// The index of the first sample in a source taken after the given moment, or
// -1 when there is none. A binary search, so a check picks up where it left
// off without walking the whole store past everything it has already seen.
int chk_first_after(al_sample_source_t* source, int64_t since);

// Marks a phase boundary at the current moment, in the slot the check's own
// enum names for it. A check is not one contiguous window — the user is
// prompted between phases and takes as long as they take — so the boundaries
// have to be recorded as they happen rather than inferred from the durations
// afterwards; and they are named rather than appended so a step done again
// lands on the same mark instead of shifting every one after it. The payload
// carries them as sample indices, and the page bands the chart at them.
void chk_mark(chk_t* c, uint8_t slot);

// Milliseconds since the check began.
int32_t chk_elapsed(const chk_t* c);

// Selects the language for check copy, using the same indices as scr_lang_t.
void chk_init(int lang);

// Names the screen the running flow is on, which a measurement or a prompt
// left alone parks the device on and wakes back into. Each flow sets it on
// entry and chk_release forgets it, so a result shown from the past list,
// which runs no flow, never parks.
void chk_park_into(void* screen);

// Returns a check string, falling back to English when the selected language
// has none. The offset is a byte offset into chk_trans_t, as produced by
// offsetof; CHK_TEXT wraps it.
const char* chk_text_at(size_t offset);
#define CHK_TEXT(field) chk_text_at(offsetof(chk_trans_t, field))

// Adds one observation to a least-squares accumulator.
void chk_accum_add(chk_accum_t* a, double x, double y);

// Resets an accumulator to empty.
void chk_accum_reset(chk_accum_t* a);

// Solves an accumulator for the slope of y over x and the coefficient of
// determination. Returns false when there are fewer than min_n observations
// or the terms are degenerate, in which case slope and r2 are untouched.
bool chk_accum_fit(const chk_accum_t* a, int min_n, float* slope, float* r2);

// Solves for the standard error of the slope, which is the band a result
// carries. Returns false under the same conditions as chk_accum_fit, and
// additionally when there are fewer than three observations.
bool chk_accum_stderr(const chk_accum_t* a, float* stderr_slope);

// The measurement engine covers a baseline and a measurement both: they are
// the same loop, differing only in when they stop. The drawing lives in the
// screen; what follows is the policy, kept separate so it can be tested.

// What the engine does next.
typedef enum {
  CHK_RUN_GO,      // keep sampling
  CHK_RUN_DONE,    // stop, the run stands
  CHK_RUN_FAILED,  // stop, too many readings were unusable
} chk_run_state_t;

// A run is abandoned once more than a quarter of the attempts have failed,
// and not before there have been enough attempts for that to mean anything.
#define CHK_FAIL_MIN_ATTEMPTS 8
#define CHK_FAIL_SHARE 4

// Clears a run.
void chk_measure_reset(chk_measure_run_t* r);

// Folds one reading into a run and says what to do next. `valid` is whether
// the sensor gave a usable number; `verdict` is what the check made of it,
// and is ignored when it did not.
chk_run_state_t chk_measure_step(const chk_measure_cfg_t* cfg, chk_measure_run_t* r, int32_t elapsed, bool valid,
                                 chk_step_t verdict);

// Folds one usable reading into the settle classifier and returns its verdict:
// going while the windows still differ, done once two agree. A run that does
// not settle passes the check's own verdict through untouched.
chk_step_t chk_measure_classify(const chk_measure_cfg_t* cfg, chk_measure_run_t* r, float value, int32_t elapsed,
                                chk_step_t verdict);

// The value a settled run stands for: the median of its newest complete
// window, NaN before there is one. Meaningful whether or not the run actually
// settled, which chk_settle_done says.
float chk_settle_value(const chk_measure_run_t* r);
bool chk_settle_done(const chk_measure_run_t* r);

// Estimates how long a settling run has left, in ms. The change between the
// last two windows shrinks by a fixed factor each window as the sensor's
// exponential catches up, so the time until it drops under the band is the time
// constant times the log of their ratio. Clamped so it never promises an end
// before the floor or after the cap, and it re-estimates each window, so it
// corrects itself when the air or the sensor is slower than the datasheet.
// Before two windows exist it is what is left of the floor. -1 for a run that
// does not settle.
int32_t chk_measure_remaining(const chk_measure_cfg_t* cfg, const chk_measure_run_t* r, int32_t elapsed);

// Holds an estimate of what a run has left to the limits its config sets: it
// may promise an end no earlier than the floor, none later than the cap, and
// never one in the past. A rough estimate held this way is wrong by at most
// the span between the two.
int32_t chk_measure_bound(const chk_measure_cfg_t* cfg, int32_t left, int32_t elapsed);

// How a run shows itself: a baseline counts samples towards a target, a
// measurement draws the signal falling or rising over time.
typedef enum {
  CHK_SHOW_PROGRESS,
  CHK_SHOW_CHART,
} chk_show_t;

// What a check makes of each reading.
typedef chk_step_t (*chk_sample_fn)(chk_t* c, float value, int32_t t_ms);

// What a check makes of how much longer it needs, in ms, or -1 while it
// cannot say. A check whose run ends on its own reading of the signal knows
// this better than the settle classifier does, and says so here.
typedef int32_t (*chk_remaining_fn)(chk_t* c);

typedef struct {
  const char* title;
  const char* stage;
  const char* hint;   // under the value, or NULL
  const char* nudge;  // shown instead once the run is prompting, or NULL
  const char* unit;   // "ppm", "ug/m3"
  chk_show_t show;
  const char* back;         // the B key's label, or NULL to leave the key unlabelled
  al_sample_field_t field;  // which signal to read
  chk_measure_cfg_t cfg;
  chk_sample_fn on_sample;
  chk_remaining_fn on_remaining;  // the check's own estimate, or NULL for the classifier's
  float floor;                    // the value a zero-height bar stands for
  float range;                    // the span the chart covers above the floor, 0 to size it
  // A run the signal itself never ends is ended by the user, and the B key's
  // label says so. Past the floor the key stands for the run being over;
  // before it the run is handed back to the flow, which asks whether to stop
  // the check at all, since a run that short is more likely a slip.
  bool ends_on_key;
  int32_t slot_ms;    // the time one chart slot covers, 0 for CHK_SLOT_MS
  int32_t redraw_ms;  // the least time between panel redraws, 0 to redraw on every reading
} chk_screen_t;

// Seconds between the samples a check reads, which is the sensor's own
// cadence rather than anything the stores are configured to.
int chk_cadence(void);

// Seconds between the readings a check keeps on its record, which is the
// device's cadence unless the check asked for a slower one: a night at five
// seconds is a hundred times the sample cap, so a long check thins its
// readings onto the record while every one of them still feeds the evaluator.
int chk_record_cadence(const chk_t* c);

// The clock's UTC offset in minutes east at a moment, from the zone the
// device's time is set to. Whether a zone was set at all is the caller's to
// know; this reports what the clock does.
int16_t chk_utc_offset(int64_t epoch_ms);

// Runs a measurement: draws the screen, samples at the device's cadence,
// feeds each reading to the check, and stops when the policy says so. The
// run is filled in as it goes, so the caller can see what happened. A run
// that has already been entered (its `began` is set) is continued rather
// than started, which is how a measurement picks up after a deep sleep. At a
// slow cadence the wait between readings is spent in a deep sleep that wakes
// back into the flow's own screen, so the run continues there. The B key
// leaves the run as it is and reports back; the flow decides whether that
// is a restart, a stop, or nothing.
chk_result_t chk_measure(chk_t* c, const chk_screen_t* screen);

// Runs the ventilation check. The three arguments are the screens each
// outcome lands on: leaving, timing out, and starting over.
void* chk_vent_run(void* on_exit, void* on_idle, void* self);

// Runs the gas stove check, the same way.
void* chk_stove_run(void* on_exit, void* on_idle, void* self);

// Runs the bedroom night check, the same way.
void* chk_bedroom_run(void* on_exit, void* on_idle, void* self);

// Runs the bathroom humidity check, the same way.
void* chk_bath_run(void* on_exit, void* on_idle, void* self);

// the bubbles a verdict has
#define CHK_VERDICT_MAX 2

// A finished check, rebuilt from its result block. The same view serves the
// live flow and a reopened one, so the two cannot drift apart. Note the
// ceiling: lvx_fmt rotates eight buffers, so a view may not hold more strings
// than that at once.
typedef struct {
  const char* title;
  uint8_t check;  // the payload's check id, a chk_code_check_t
  al_sample_field_t signal;
  chk_bubble_t verdict[CHK_VERDICT_MAX];  // what Robin says about the result, then the advice
  size_t num_verdict;
  const char* lines[6];
  size_t num_lines;
  const char* note;
  float payload[CHK_CODE_MAX_FIELDS];
  size_t num_payload;
  uint16_t marks[CHK_MARKS];  // the phase boundaries as sample indices, in the check's slot order
  size_t num_marks;
} chk_view_t;

// The name a check goes by in menus, shorter than the title its screens
// carry. NULL for an unknown check.
const char* chk_name(uint8_t id);

// Fills a view from a check's result block. False for an unknown check.
bool chk_describe(uint8_t id, const float* result, const int32_t* marks, uint8_t marks_len, uint8_t cadence,
                  chk_view_t* out);

// Seals the record of a finished check and returns its number, or zero when
// there was nothing worth keeping. The record was opened and appended to as
// each measurement ended, so this copies out only what the store holds since
// the last one; a check keeps accumulators rather than a sample stream, and
// the record is where its curve comes from. Called as soon as the result is
// evaluated rather than when it is shown, so that walking away from the
// verdict does not lose it, and a result step re-entered afterwards gets the
// same number back rather than a second record.
uint16_t chk_record(chk_t* c, al_sample_field_t signal);

// Fills a view from a stored check, using the cadence it was recorded at
// rather than whatever the device is set to now. A live flow describes its
// result this way too: having just written the check, it reads it back, so
// the result shown now and the same result reopened later cannot differ.
bool chk_view_of(uint16_t num, chk_view_t* out);

// Draws the code for a stored check, rebuilt from its header and samples.
chk_result_t chk_show_code(uint16_t num);

// Shows a stored check again: its verdict, its stats, then its code, the
// screens the live flow ends on. Nothing is re-run.
chk_result_t chk_reopen(uint16_t num);

// Converts a first-order decay rate in air changes per hour into the two
// figures Persily 1997 defines: the half-life of the stale air, and the time
// to 95 per cent fresh, which is three time constants. Both in minutes, and
// both meaningless for a rate at or below zero, which they report as -1.
float chk_half_life(float ach);
float chk_fresh_time(float ach);

// Propagates the band on a rate onto its half-life, since half-life is not
// linear in the rate. Minutes, or -1 when the rate is not positive.
float chk_half_life_band(float ach, float ach_band);

// Rounds a duration in minutes to something worth saying out loud: whole
// minutes below ten, five-minute steps below forty-five, ten above.
int chk_round_minutes(float minutes);

// The median of n values, which are reordered in the process.
float chk_median(float* values, int n);

#endif  // CHK_H
