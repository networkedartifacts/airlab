#include <math.h>
#include <stddef.h>
#include <string.h>

#include "chk.h"
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
