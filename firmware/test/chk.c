#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <unity.h>

#include "chk.h"
#include "scr_trans.inc"
#include "chk_trans.inc"

#define NUM_LANGS (sizeof(chk_trans_map) / sizeof(chk_trans_t))
#define NUM_STRINGS (sizeof(chk_trans_t) / sizeof(const char*))

// The checks ship English-only for the first trial, so this is the weaker
// rule scr_trans.inc does not get: English must be complete, the other
// languages may be absent entirely, and anything they do set must not be
// blank. chk_text_at() falls back to English for the rest.
// chk_init() is handed scr_lang(), so the two enums have to agree.
static void test_language_order_matches_scr() {
  _Static_assert((int)CHK_DE == (int)SCR_DE, "check language order must match scr_lang_t");
  _Static_assert((int)CHK_EN == (int)SCR_EN, "check language order must match scr_lang_t");
  _Static_assert((int)CHK_ES == (int)SCR_ES, "check language order must match scr_lang_t");
  _Static_assert((int)CHK_FR == (int)SCR_FR, "check language order must match scr_lang_t");
  TEST_ASSERT_EQUAL_INT(SCR_EN, CHK_EN);
}

static void test_english_is_complete() {
  _Static_assert(sizeof(chk_trans_t) % sizeof(const char*) == 0, "chk_trans_t must only contain strings");

  const char* const* english = (const char* const*)&chk_trans_map[CHK_EN];
  int missing = 0;
  for (size_t s = 0; s < NUM_STRINGS; s++) {
    if (english[s] == NULL || english[s][0] == '\0') {
      printf("check string %zu is missing in English\n", s);
      missing++;
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, missing, "every check string must be set in English");
}

static void test_other_languages_are_not_blank() {
  int blank = 0;
  for (size_t l = 0; l < NUM_LANGS; l++) {
    if (l == CHK_EN) {
      continue;
    }
    const char* const* strings = (const char* const*)&chk_trans_map[l];
    for (size_t s = 0; s < NUM_STRINGS; s++) {
      if (strings[s] != NULL && strings[s][0] == '\0') {
        printf("check string %zu is blank in language %zu\n", s, l);
        blank++;
      }
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, blank, "a translated string must not be empty");
}

static void test_missing_translation_falls_back_to_english() {
  // German is not populated yet, so every lookup must yield the English text
  chk_init(SCR_DE);
  TEST_ASSERT_EQUAL_STRING(chk_trans_map[CHK_EN].vent__intro_1, CHK_TEXT(vent__intro_1));

  chk_init(CHK_EN);
  TEST_ASSERT_EQUAL_STRING(chk_trans_map[CHK_EN].vent__intro_1, CHK_TEXT(vent__intro_1));

  // an out-of-range language must not read past the table
  chk_init(99);
  TEST_ASSERT_EQUAL_STRING(chk_trans_map[CHK_EN].vent__intro_1, CHK_TEXT(vent__intro_1));
}

// The array-based fit the checks-demo branch carries, kept here as the
// reference the streaming accumulator has to agree with.
static bool reference_fit(const double* xs, const double* ys, int count, float* slope, float* r2) {
  double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
  for (int i = 0; i < count; i++) {
    sx += xs[i], sy += ys[i], sxx += xs[i] * xs[i], sxy += xs[i] * ys[i], syy += ys[i] * ys[i];
  }
  double dx = count * sxx - sx * sx;
  double dy = count * syy - sy * sy;
  if (dx <= 0 || dy <= 0) {
    return false;
  }
  *slope = (float)((count * sxy - sx * sy) / dx);
  *r2 = (float)((count * sxy - sx * sy) * (count * sxy - sx * sy) / (dx * dy));
  return true;
}

static void test_streaming_fit_matches_the_array_fit() {
  // a CO2 decay: 843 ppm falling towards 425 at about 2.9 air changes an hour,
  // sampled every 5 s for six minutes, with a little noise on each reading
  double xs[72], ys[72];
  chk_accum_t a;
  chk_accum_reset(&a);
  for (int i = 0; i < 72; i++) {
    double t = i * 5.0 / 3600.0;                       // hours
    double excess = (843.0 - 425.0) * exp(-2.9 * t);   // ppm above outdoor
    double noise = ((i * 37) % 11 - 5) * 0.4;          // deterministic, +/- 2 ppm
    xs[i] = t;
    ys[i] = log(excess + noise);
    chk_accum_add(&a, xs[i], ys[i]);
  }

  float ref_slope, ref_r2, got_slope, got_r2;
  TEST_ASSERT_TRUE(reference_fit(xs, ys, 72, &ref_slope, &ref_r2));
  TEST_ASSERT_TRUE(chk_accum_fit(&a, 12, &got_slope, &got_r2));

  TEST_ASSERT_FLOAT_WITHIN(1e-4, ref_slope, got_slope);
  TEST_ASSERT_FLOAT_WITHIN(1e-4, ref_r2, got_r2);

  // and the slope is the air exchange rate it was built from
  TEST_ASSERT_FLOAT_WITHIN(0.1, -2.9, got_slope);
  TEST_ASSERT_TRUE(got_r2 > 0.99);
}

static void test_fit_refuses_too_few_samples() {
  chk_accum_t a;
  chk_accum_reset(&a);
  for (int i = 0; i < 5; i++) {
    chk_accum_add(&a, i, 2.0 * i + 1.0);
  }

  float slope = 123, r2 = 456;
  TEST_ASSERT_FALSE(chk_accum_fit(&a, 12, &slope, &r2));
  TEST_ASSERT_EQUAL_FLOAT(123, slope);  // untouched on refusal
  TEST_ASSERT_EQUAL_FLOAT(456, r2);
}

static void test_fit_refuses_a_flat_signal() {
  // no variation in y: R^2 is undefined and the check has learned nothing
  chk_accum_t a;
  chk_accum_reset(&a);
  for (int i = 0; i < 20; i++) {
    chk_accum_add(&a, i, 6.0);
  }

  float slope, r2;
  TEST_ASSERT_FALSE(chk_accum_fit(&a, 12, &slope, &r2));
}

static void test_stderr_is_zero_on_an_exact_line_and_grows_with_scatter() {
  chk_accum_t exact;
  chk_accum_reset(&exact);
  for (int i = 0; i < 20; i++) {
    chk_accum_add(&exact, i, -3.0 * i + 7.0);
  }
  float se_exact;
  TEST_ASSERT_TRUE(chk_accum_stderr(&exact, &se_exact));
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 0.0, se_exact);

  chk_accum_t noisy;
  chk_accum_reset(&noisy);
  for (int i = 0; i < 20; i++) {
    chk_accum_add(&noisy, i, -3.0 * i + 7.0 + ((i * 7) % 5 - 2));
  }
  float se_noisy;
  TEST_ASSERT_TRUE(chk_accum_stderr(&noisy, &se_noisy));
  TEST_ASSERT_TRUE(se_noisy > se_exact);
}

static void test_stderr_refuses_without_a_degree_of_freedom() {
  chk_accum_t a;
  chk_accum_reset(&a);
  chk_accum_add(&a, 0, 1);
  chk_accum_add(&a, 1, 2);

  float se;
  TEST_ASSERT_FALSE(chk_accum_stderr(&a, &se));
}

// a source of evenly spaced samples, standing in for the device store
typedef struct {
  int64_t start;
  int count;
  int32_t step;
} fake_source_t;

static al_sample_info_t fake_source_info(void* ctx) {
  fake_source_t* f = ctx;
  return (al_sample_info_t){.start = f->start, .length = (f->count - 1) * f->step, .count = (size_t)f->count};
}

static void fake_source_read(void* ctx, al_sample_t* samples, size_t num, size_t offset) {
  fake_source_t* f = ctx;
  for (size_t i = 0; i < num; i++) {
    samples[i] = (al_sample_t){.off = (int32_t)(offset + i) * f->step};
  }
}

static void test_first_after_lands_on_the_next_sample() {
  // a hundred samples five seconds apart, the first at t0
  fake_source_t f = {.start = 1000000, .count = 100, .step = 5000};
  al_sample_source_t src = {.ctx = &f, .info = fake_source_info, .read = fake_source_read};

  // before the store: everything is new
  TEST_ASSERT_EQUAL_INT(0, chk_first_after(&src, 0));
  TEST_ASSERT_EQUAL_INT(0, chk_first_after(&src, f.start - 1));

  // a moment equal to a sample is not after it
  TEST_ASSERT_EQUAL_INT(1, chk_first_after(&src, f.start));
  TEST_ASSERT_EQUAL_INT(3, chk_first_after(&src, f.start + 10000));

  // between two samples: the later one
  TEST_ASSERT_EQUAL_INT(3, chk_first_after(&src, f.start + 12500));

  // the newest has been seen, so there is nothing
  TEST_ASSERT_EQUAL_INT(99, chk_first_after(&src, f.start + 99 * 5000 - 1));
  TEST_ASSERT_EQUAL_INT(-1, chk_first_after(&src, f.start + 99 * 5000));
  TEST_ASSERT_EQUAL_INT(-1, chk_first_after(&src, f.start + 1000000));

  // and an empty store has nothing either
  fake_source_t empty = {.start = 0, .count = 0, .step = 5000};
  al_sample_source_t none = {.ctx = &empty, .info = fake_source_info, .read = fake_source_read};
  TEST_ASSERT_EQUAL_INT(-1, chk_first_after(&none, 0));
}

static void test_utc_offset_follows_the_clock_zone() {
  // 2026-09-17T10:00:00Z, summer in Europe; and 2026-11-26T10:40:00Z, winter
  const int64_t summer = 1789639200000LL;
  const int64_t winter = 1795689600000LL;

  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  TEST_ASSERT_EQUAL_INT16(120, chk_utc_offset(summer));
  TEST_ASSERT_EQUAL_INT16(60, chk_utc_offset(winter));

  // a half-hour zone, and one west of UTC
  setenv("TZ", "IST-5:30", 1);
  tzset();
  TEST_ASSERT_EQUAL_INT16(330, chk_utc_offset(summer));
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
  tzset();
  TEST_ASSERT_EQUAL_INT16(-240, chk_utc_offset(summer));
  TEST_ASSERT_EQUAL_INT16(-300, chk_utc_offset(winter));

  // across a year end, where the day of the year says nothing: Auckland at
  // 2026-12-31T14:00:00Z is already 3 a.m. on New Year's Day, and Honolulu at
  // 2027-01-01T05:00:00Z is still the evening of the year before
  setenv("TZ", "NZST-12NZDT,M9.5.0,M4.1.0/3", 1);
  tzset();
  TEST_ASSERT_EQUAL_INT16(780, chk_utc_offset(1798725600000LL));
  setenv("TZ", "HST10", 1);
  tzset();
  TEST_ASSERT_EQUAL_INT16(-600, chk_utc_offset(1798779600000LL));

  // and UTC itself
  setenv("TZ", "UTC0", 1);
  tzset();
  TEST_ASSERT_EQUAL_INT16(0, chk_utc_offset(summer));
}

void suite_chk() {
  RUN_TEST(test_utc_offset_follows_the_clock_zone);
  RUN_TEST(test_first_after_lands_on_the_next_sample);
  RUN_TEST(test_language_order_matches_scr);
  RUN_TEST(test_english_is_complete);
  RUN_TEST(test_other_languages_are_not_blank);
  RUN_TEST(test_missing_translation_falls_back_to_english);
  RUN_TEST(test_streaming_fit_matches_the_array_fit);
  RUN_TEST(test_fit_refuses_too_few_samples);
  RUN_TEST(test_fit_refuses_a_flat_signal);
  RUN_TEST(test_stderr_refuses_without_a_degree_of_freedom);
  RUN_TEST(test_stderr_is_zero_on_an_exact_line_and_grows_with_scatter);
}
