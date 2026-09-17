#include <math.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include <al/clock.h>
#include <al/sensor.h>

#include "chk.h"
#include "dev.h"
#include "chk_trans.inc"

#define CHK_NUM_LANGS (sizeof(chk_trans_map) / sizeof(chk_trans_t))

static int chk_lang = CHK_EN;

void chk_init(int lang) {
  // keep the language, ignoring one we have no column for
  chk_lang = (lang >= 0 && (size_t)lang < CHK_NUM_LANGS) ? lang : CHK_EN;
}

const char *chk_text_at(size_t offset) {
  // read the field out of the selected language
  const char *const *selected = (const char *const *)((const char *)&chk_trans_map[chk_lang] + offset);
  if (*selected != NULL) {
    return *selected;
  }

  // fall back to English, which chk_trans.inc always fills
  const char *const *english = (const char *const *)((const char *)&chk_trans_map[CHK_EN] + offset);
  return *english;
}

void chk_accum_reset(chk_accum_t *a) {
  memset(a, 0, sizeof(*a));
}

void chk_accum_add(chk_accum_t *a, double x, double y) {
  a->n++;
  a->sx += x;
  a->sy += y;
  a->sxx += x * x;
  a->sxy += x * y;
  a->syy += y * y;
}

// the shared denominators, false when the terms cannot support a fit
static bool chk_accum_terms(const chk_accum_t *a, int min_n, double *dx, double *dy, double *sxy) {
  if (a->n < min_n || a->n < 2) {
    return false;
  }
  *dx = a->n * a->sxx - a->sx * a->sx;
  *dy = a->n * a->syy - a->sy * a->sy;
  *sxy = a->n * a->sxy - a->sx * a->sy;
  return *dx > 0 && *dy > 0;
}

bool chk_accum_fit(const chk_accum_t *a, int min_n, float *slope, float *r2) {
  // compute terms
  double dx, dy, sxy;
  if (!chk_accum_terms(a, min_n, &dx, &dy, &sxy)) {
    return false;
  }

  // compute slope and R^2
  *slope = (float)(sxy / dx);
  *r2 = (float)(sxy * sxy / (dx * dy));

  return true;
}

bool chk_accum_stderr(const chk_accum_t *a, float *stderr_slope) {
  // compute terms, a standard error needs one degree of freedom beyond the fit
  double dx, dy, sxy;
  if (a->n < 3 || !chk_accum_terms(a, 3, &dx, &dy, &sxy)) {
    return false;
  }

  // residual sum of squares, from the same accumulated terms:
  //   RSS = (dy - sxy^2 / dx) / n
  double rss = (dy - sxy * sxy / dx) / a->n;
  if (rss < 0) {
    rss = 0;  // rounding can take an exact fit just below zero
  }

  // the slope's standard error is sqrt(RSS / (n - 2) * n / dx)
  *stderr_slope = (float)sqrt(rss * a->n / ((a->n - 2) * dx));

  return true;
}

bool chk_available(uint16_t needs) {
  // the particulate sensor is the only optional signal so far
  if ((needs & CHK_NEEDS_PM) != 0 && !al_sensor_pm_present()) {
    return false;
  }
  return true;
}

float chk_half_life(float ach) {
  // a rate at or below zero describes no decay at all
  if (ach <= 0) {
    return -1;
  }

  // ln(2) / ACH, in minutes
  return (float)(M_LN2 / ach * 60.0);
}

float chk_fresh_time(float ach) {
  // as above
  if (ach <= 0) {
    return -1;
  }

  // three time constants leaves 5 % of the excess, which Persily 1997 calls
  // 95 % fresh: ln(20) / ACH, in minutes
  return (float)(log(20.0) / ach * 60.0);
}

float chk_half_life_band(float ach, float ach_band) {
  // as above
  if (ach <= 0) {
    return -1;
  }

  // half-life goes as 1/ACH, so the band scales by its derivative, ln(2)/ACH^2
  return (float)(M_LN2 / (ach * ach) * ach_band * 60.0);
}

int16_t chk_utc_offset(int64_t epoch_ms) {
  time_t t = (time_t)(epoch_ms / 1000);
  struct tm local, utc;
  localtime_r(&t, &local);
  gmtime_r(&t, &utc);

  // an offset is within a day of UTC, so the calendars differ by at most one
  // day, and across a year end the day of the year says nothing
  int days = local.tm_yday - utc.tm_yday;
  if (local.tm_year != utc.tm_year) {
    days = local.tm_year > utc.tm_year ? 1 : -1;
  }
  return (int16_t)(days * 1440 + (local.tm_hour - utc.tm_hour) * 60 + (local.tm_min - utc.tm_min));
}

int chk_round_minutes(float minutes) {
  if (minutes < 10) {
    return minutes < 1 ? 1 : (int)(minutes + 0.5f);
  } else if (minutes < 45) {
    return (int)(minutes / 5 + 0.5f) * 5;
  }
  return (int)(minutes / 10 + 0.5f) * 10;
}

float chk_median(float *values, int n) {
  if (n <= 0) {
    return NAN;
  }

  // insertion sort, which is plenty for a handful of readings
  for (int i = 1; i < n; i++) {
    for (int j = i; j > 0 && values[j] < values[j - 1]; j--) {
      float tmp = values[j];
      values[j] = values[j - 1];
      values[j - 1] = tmp;
    }
  }

  // the middle, or the mean of the two middles
  if (n % 2 == 1) {
    return values[n / 2];
  }
  return (values[n / 2 - 1] + values[n / 2]) / 2;
}

void chk_measure_reset(chk_measure_run_t *r) {
  memset(r, 0, sizeof(*r));
  r->settle.last = NAN;
  r->settle.prior = NAN;
  r->settle.eta = -1;
}

chk_step_t chk_measure_classify(const chk_measure_cfg_t *cfg, chk_measure_run_t *r, float value, int32_t elapsed,
                                chk_step_t verdict) {
  if (!cfg->settle) {
    return verdict;
  }
  chk_settle_t *s = &r->settle;

  // fill the window, and say nothing new until it is full
  s->window[s->fill++] = value;
  if (s->fill < CHK_SETTLE_WINDOW) {
    return s->settled ? CHK_STEP_DONE : CHK_STEP_GO;
  }

  // reduce it to its median and shift the windows along
  float sorted[CHK_SETTLE_WINDOW];
  memcpy(sorted, s->window, sizeof(sorted));
  s->prior = s->last;
  s->last = chk_median(sorted, CHK_SETTLE_WINDOW);
  s->fill = 0;

  // one window says nothing about whether the reading holds
  if (isnan(s->prior)) {
    s->settled = false;
    s->eta = -1;
    return CHK_STEP_GO;
  }

  // settled when two windows in a row agree to within the band
  float delta = fabsf(s->last - s->prior);
  s->settled = delta <= CHK_SETTLE_BAND;

  // the change per window falls by a fixed factor each window, so the time
  // until it is under the band is the time constant times the log of the ratio
  if (s->settled) {
    s->eta = elapsed;
  } else {
    s->eta = elapsed + (int32_t)(CHK_SETTLE_TAU_MS * log(delta / CHK_SETTLE_BAND));
  }

  return s->settled ? CHK_STEP_DONE : CHK_STEP_GO;
}

float chk_settle_value(const chk_measure_run_t *r) {
  return r->settle.last;
}

bool chk_settle_done(const chk_measure_run_t *r) {
  return r->settle.settled;
}

int32_t chk_measure_remaining(const chk_measure_cfg_t *cfg, const chk_measure_run_t *r, int32_t elapsed) {
  if (!cfg->settle) {
    return -1;
  }

  // what the last two windows promise, or the floor while there are not two
  int32_t left = r->settle.eta >= 0 ? r->settle.eta - elapsed : cfg->min_ms - elapsed;

  // never before the floor, never after the cap
  if (left < cfg->min_ms - elapsed) {
    left = cfg->min_ms - elapsed;
  }
  if (cfg->max_ms > 0 && left > cfg->max_ms - elapsed) {
    left = cfg->max_ms - elapsed;
  }
  if (left < 0) {
    left = 0;
  }

  return left;
}

chk_run_state_t chk_measure_step(const chk_measure_cfg_t *cfg, chk_measure_run_t *r, int32_t elapsed, bool valid,
                                 chk_step_t verdict) {
  // record the attempt
  r->elapsed = elapsed;
  r->attempts++;
  if (valid) {
    r->count++;
  } else {
    r->fails++;
  }

  // give up once too many readings have come back unusable, but not before
  // there have been enough of them to tell a bad sensor from a slow start
  if (r->attempts >= CHK_FAIL_MIN_ATTEMPTS && r->fails * CHK_FAIL_SHARE > r->attempts) {
    return CHK_RUN_FAILED;
  }

  // let the check stop the run, but never before the minimum duration: a
  // fit needs a span of time whatever the signal happens to have done
  if (valid && verdict == CHK_STEP_DONE && elapsed >= cfg->min_ms) {
    return CHK_RUN_DONE;
  }

  // stop at the limits
  if (cfg->max_ms > 0 && elapsed >= cfg->max_ms) {
    return CHK_RUN_DONE;
  }
  if (cfg->capacity > 0 && r->count >= cfg->capacity) {
    return CHK_RUN_DONE;
  }

  // prompt while nothing is moving, and stop prompting once something does.
  // an unusable reading says nothing either way, so it leaves this alone.
  if (valid && cfg->nudge_ms > 0) {
    r->nudging = elapsed >= cfg->nudge_ms && verdict == CHK_STEP_WAIT;
  }

  return CHK_RUN_GO;
}

// The check in progress. RTC-retained, so it survives the deep sleep that a
// long check spends most of its time in — and is lost on a crash, which is
// the line the design draws.
DEV_KEEP static chk_t chk_current;

chk_t *chk_context(void) {
  return &chk_current;
}

bool chk_resuming(const chk_t *c, uint8_t id) {
  return c->id == id && c->start != 0;
}

bool chk_started(const chk_t *c) {
  return c->seen != 0;
}

void chk_begin(chk_t *c, uint8_t id) {
  memset(c, 0, sizeof(*c));
  c->id = id;
  c->step = 0;
  c->start = al_clock_get_epoch();
}

void chk_end(chk_t *c) {
  memset(c, 0, sizeof(*c));
  c->id = CHK_NONE;
}

int32_t chk_elapsed(const chk_t *c) {
  return (int32_t)(al_clock_get_epoch() - c->start);
}

int chk_first_after(al_sample_source_t *source, int64_t since) {
  al_sample_info_t info = source->info(source->ctx);
  if (info.count == 0) {
    return -1;
  }

  // offsets are relative to the oldest sample: anything at or before the
  // moment is not wanted, and nothing is newer than the newest
  int64_t off = since + 1 - info.start;
  if (off < 0) {
    off = 0;
  }
  if (off > info.length) {
    return -1;
  }

  int32_t offset = (int32_t)off;
  return al_sample_search(source, &offset);
}

void chk_mark(chk_t *c) {
  // find the first free slot, ignoring the mark once they are all taken
  for (size_t i = 0; i < CHK_MARKS; i++) {
    if (c->marks[i] == 0) {
      c->marks[i] = chk_elapsed(c);
      return;
    }
  }
}
