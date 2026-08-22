#include <math.h>

#include <unity.h>

#include <al/sample.h>

/* Fake Source */

#define SRC_MAX 64

static al_sample_t src_data[SRC_MAX];
static size_t src_num;

static size_t src_count(void* ctx) { return src_num; }

static void src_read(void* ctx, al_sample_t* samples, size_t num, size_t offset) {
  for (size_t i = 0; i < num; i++) {
    samples[i] = src_data[offset + i];
  }
}

static al_sample_source_t src = {.count = src_count, .read = src_read};

static void src_fill(const int32_t* offsets, const int16_t* co2, size_t num) {
  src_num = num;
  for (size_t i = 0; i < num; i++) {
    src_data[i] = (al_sample_t){.off = offsets[i], .co2 = co2[i]};
  }
}

/* Tests */

static void test_sample_valid() {
  TEST_ASSERT_FALSE(al_sample_valid((al_sample_t){.co2 = 0}));
  TEST_ASSERT_TRUE(al_sample_valid((al_sample_t){.co2 = 400}));
}

static void test_sample_read() {
  // prepare sample with flagged gas and PM values
  al_sample_t sample = {
      .off = 1234,
      .co2 = 456,
      .tmp = 2345,
      .hum = 5678,
      .voc = 123 | AL_SAMPLE_GAS_LEARNING,
      .nox = 0 | AL_SAMPLE_GAS_CYCLED,
      .prs = 950,
      .pm = 123 | AL_SAMPLE_PM_OBSTRUCTED,
  };

  // verify scaling and flag masking
  TEST_ASSERT_EQUAL_FLOAT(456.f, al_sample_read(sample, AL_SAMPLE_CO2));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.45f, al_sample_read(sample, AL_SAMPLE_TMP));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 56.78f, al_sample_read(sample, AL_SAMPLE_HUM));
  TEST_ASSERT_EQUAL_FLOAT(123.f, al_sample_read(sample, AL_SAMPLE_VOC));
  TEST_ASSERT_FLOAT_IS_NAN(al_sample_read(sample, AL_SAMPLE_NOX));
  TEST_ASSERT_EQUAL_FLOAT(950.f, al_sample_read(sample, AL_SAMPLE_PRS));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.3f, al_sample_read(sample, AL_SAMPLE_PM));
  TEST_ASSERT_EQUAL_FLOAT(1234.f, al_sample_read(sample, AL_SAMPLE_OFF));

  // verify no-data sentinels
  TEST_ASSERT_FLOAT_IS_NAN(al_sample_read((al_sample_t){.voc = 0}, AL_SAMPLE_VOC));
  TEST_ASSERT_FLOAT_IS_NAN(al_sample_read((al_sample_t){.pm = -1}, AL_SAMPLE_PM));
}

static void test_sample_altitude() {
  // prepare sample
  al_sample_t sample = {.prs = 950};

  // verify sea-level correction at 500m (ISA factor ~0.9421)
  al_sample_set_altitude(500.f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 1008.3f, al_sample_read(sample, AL_SAMPLE_PRS));

  // verify clamping to the sensible range
  al_sample_set_altitude(20000.f);
  float high = al_sample_read(sample, AL_SAMPLE_PRS);
  al_sample_set_altitude(9000.f);
  TEST_ASSERT_EQUAL_FLOAT(al_sample_read(sample, AL_SAMPLE_PRS), high);
  al_sample_set_altitude(-1000.f);
  float low = al_sample_read(sample, AL_SAMPLE_PRS);
  al_sample_set_altitude(-500.f);
  TEST_ASSERT_EQUAL_FLOAT(al_sample_read(sample, AL_SAMPLE_PRS), low);

  // verify zero disables the correction
  al_sample_set_altitude(0.f);
  TEST_ASSERT_EQUAL_FLOAT(950.f, al_sample_read(sample, AL_SAMPLE_PRS));
}

static void test_sample_flags() {
  TEST_ASSERT_EQUAL_INT32(AL_SAMPLE_GAS_LEARNING,
                          al_sample_flags((al_sample_t){.voc = 100 | AL_SAMPLE_GAS_LEARNING}, AL_SAMPLE_VOC));
  TEST_ASSERT_EQUAL_INT32(AL_SAMPLE_GAS_CYCLED,
                          al_sample_flags((al_sample_t){.nox = 5 | AL_SAMPLE_GAS_CYCLED}, AL_SAMPLE_NOX));
  TEST_ASSERT_EQUAL_INT32(AL_SAMPLE_PM_OBSTRUCTED,
                          al_sample_flags((al_sample_t){.pm = 10 | AL_SAMPLE_PM_OBSTRUCTED}, AL_SAMPLE_PM));
  TEST_ASSERT_EQUAL_INT32(0, al_sample_flags((al_sample_t){.pm = -1}, AL_SAMPLE_PM));
  TEST_ASSERT_EQUAL_INT32(0, al_sample_flags((al_sample_t){.co2 = 400}, AL_SAMPLE_CO2));
}

static void test_sample_lerp() {
  // prepare endpoints
  al_sample_t a = {
      .off = 0,
      .co2 = 400,
      .tmp = 2000,
      .hum = 4000,
      .voc = 100 | AL_SAMPLE_GAS_LEARNING,
      .nox = 10,
      .prs = 900,
      .pm = 100,
  };
  al_sample_t b = {
      .off = 1000,
      .co2 = 600,
      .tmp = 3000,
      .hum = 5000,
      .voc = 200 | AL_SAMPLE_GAS_CYCLED,
      .nox = 0,
      .prs = 1000,
      .pm = 200 | AL_SAMPLE_PM_OBSTRUCTED,
  };

  // verify midpoint interpolation, flag combination and sentinel propagation
  al_sample_t mid = al_sample_lerp(a, b, 500);
  TEST_ASSERT_EQUAL_INT32(500, mid.off);
  TEST_ASSERT_EQUAL_INT16(500, mid.co2);
  TEST_ASSERT_EQUAL_INT16(2500, mid.tmp);
  TEST_ASSERT_EQUAL_INT16(4500, mid.hum);
  TEST_ASSERT_EQUAL_INT16(150 | AL_SAMPLE_GAS_LEARNING | AL_SAMPLE_GAS_CYCLED, mid.voc);
  TEST_ASSERT_EQUAL_INT16(0, mid.nox);
  TEST_ASSERT_EQUAL_INT16(950, mid.prs);
  TEST_ASSERT_EQUAL_INT16(150 | AL_SAMPLE_PM_OBSTRUCTED, mid.pm);

  // verify interpolation at an endpoint
  al_sample_t at_a = al_sample_lerp(a, b, 0);
  TEST_ASSERT_EQUAL_INT16(400, at_a.co2);
  TEST_ASSERT_EQUAL_INT16(2000, at_a.tmp);

  // verify PM sentinel propagation
  b.pm = -1;
  TEST_ASSERT_EQUAL_INT16(-1, al_sample_lerp(a, b, 500).pm);
}

static void test_sample_search() {
  // prepare source
  src_fill((int32_t[]){0, 1000, 2000, 3000}, (int16_t[]){400, 500, 600, 700}, 4);

  // verify exact match
  int32_t needle = 1000;
  TEST_ASSERT_EQUAL_INT(1, al_sample_search(&src, &needle));
  TEST_ASSERT_EQUAL_INT32(1000, needle);

  // verify next greater match updates the needle
  needle = 1500;
  TEST_ASSERT_EQUAL_INT(2, al_sample_search(&src, &needle));
  TEST_ASSERT_EQUAL_INT32(2000, needle);

  // verify first and before-first matches
  needle = 0;
  TEST_ASSERT_EQUAL_INT(0, al_sample_search(&src, &needle));
  needle = -100;
  TEST_ASSERT_EQUAL_INT(0, al_sample_search(&src, &needle));
  TEST_ASSERT_EQUAL_INT32(0, needle);

  // verify beyond-last and empty source
  needle = 3001;
  TEST_ASSERT_EQUAL_INT(-1, al_sample_search(&src, &needle));
  src_num = 0;
  needle = 0;
  TEST_ASSERT_EQUAL_INT(-1, al_sample_search(&src, &needle));
}

static void test_sample_count() {
  // prepare source
  src_fill((int32_t[]){0, 1000, 2000, 3000}, (int16_t[]){400, 500, 600, 700}, 4);

  // verify in-range, tail and out-of-range counts
  TEST_ASSERT_EQUAL_size_t(2, al_sample_count(&src, 0, 2000));
  TEST_ASSERT_EQUAL_size_t(4, al_sample_count(&src, 0, 4000));
  TEST_ASSERT_EQUAL_size_t(3, al_sample_count(&src, 500, 5000));
  TEST_ASSERT_EQUAL_size_t(0, al_sample_count(&src, 4000, 5000));
}

static void test_sample_query() {
  // prepare source
  src_fill((int32_t[]){0, 1000, 2000}, (int16_t[]){400, 500, 600}, 3);

  // verify interpolated query at half resolution
  al_sample_t samples[8];
  TEST_ASSERT_EQUAL_size_t(3, al_sample_query(&src, samples, 3, 0, 500));
  TEST_ASSERT_EQUAL_INT16(400, samples[0].co2);
  TEST_ASSERT_EQUAL_INT16(450, samples[1].co2);
  TEST_ASSERT_EQUAL_INT32(500, samples[1].off);
  TEST_ASSERT_EQUAL_INT16(500, samples[2].co2);

  // verify exact query and partial fill at the end
  TEST_ASSERT_EQUAL_size_t(3, al_sample_query(&src, samples, 5, 0, 1000));
  TEST_ASSERT_EQUAL_INT16(400, samples[0].co2);
  TEST_ASSERT_EQUAL_INT16(500, samples[1].co2);
  TEST_ASSERT_EQUAL_INT16(600, samples[2].co2);
  TEST_ASSERT_EQUAL_INT16(0, samples[3].co2);

  // verify query past the end
  TEST_ASSERT_EQUAL_size_t(0, al_sample_query(&src, samples, 3, 5000, 1000));

  // verify query before the start extrapolates from the first pair
  TEST_ASSERT_EQUAL_size_t(3, al_sample_query(&src, samples, 3, -1000, 1000));
  TEST_ASSERT_EQUAL_INT16(300, samples[0].co2);
  TEST_ASSERT_EQUAL_INT16(400, samples[1].co2);
  TEST_ASSERT_EQUAL_INT16(500, samples[2].co2);
}

static void test_sample_pick() {
  // prepare source
  src_fill((int32_t[]){0, 1000, 2000}, (int16_t[]){400, 600, 500}, 3);

  // verify values and min/max (max must be pre-initialized by the caller)
  float values[8] = {0};
  float min = 0, max = 0;
  TEST_ASSERT_EQUAL_size_t(3, al_sample_pick(&src, AL_SAMPLE_CO2, 3, values, &min, &max));
  TEST_ASSERT_EQUAL_FLOAT(400.f, values[0]);
  TEST_ASSERT_EQUAL_FLOAT(600.f, values[1]);
  TEST_ASSERT_EQUAL_FLOAT(500.f, values[2]);
  TEST_ASSERT_EQUAL_FLOAT(400.f, min);
  TEST_ASSERT_EQUAL_FLOAT(600.f, max);

  // verify clamping to the available count
  TEST_ASSERT_EQUAL_size_t(3, al_sample_pick(&src, AL_SAMPLE_CO2, 8, values, NULL, NULL));
}

void suite_sample() {
  RUN_TEST(test_sample_valid);
  RUN_TEST(test_sample_read);
  RUN_TEST(test_sample_altitude);
  RUN_TEST(test_sample_flags);
  RUN_TEST(test_sample_lerp);
  RUN_TEST(test_sample_search);
  RUN_TEST(test_sample_count);
  RUN_TEST(test_sample_query);
  RUN_TEST(test_sample_pick);
}
