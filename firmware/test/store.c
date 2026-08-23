#include <string.h>

#include <unity.h>

// include the unit directly to access its static state for resets
#include "../lib/store.c"

/* Helpers */

// non-static, as other suites use it to prepare the global store
void test_store_reset() {
  al_store_interval = 60;
  al_store_pos_short = 0;
  al_store_pos_long = 0;
  al_store_count_short = 0;
  al_store_count_long = 0;
  memset(al_store_short, 0, sizeof(al_store_short));
  memset(al_store_long, 0, sizeof(al_store_long));
  al_store_base = 0;
}

static void store_fill(int num, int32_t start) {
  // ingest samples 5s apart with increasing CO2 values
  for (int i = 0; i < num; i++) {
    al_store_ingest((al_sample_t){.off = start + i * 5000, .co2 = (int16_t)(400 + i)});
  }
}

/* Tests */

static void test_store_interval() {
  test_store_reset();

  // verify default and clamping to 30s-15min
  TEST_ASSERT_EQUAL_INT(60, al_store_get_interval());
  al_store_set_interval(29);
  TEST_ASSERT_EQUAL_INT(30, al_store_get_interval());
  al_store_set_interval(16 * 60);
  TEST_ASSERT_EQUAL_INT(15 * 60, al_store_get_interval());
  al_store_set_interval(120);
  TEST_ASSERT_EQUAL_INT(120, al_store_get_interval());
}

static void test_store_empty() {
  test_store_reset();

  // verify counts and zero samples
  TEST_ASSERT_EQUAL_size_t(0, al_store_count(AL_STORE_SHORT));
  TEST_ASSERT_EQUAL_size_t(0, al_store_count(AL_STORE_LONG));
  TEST_ASSERT_FALSE(al_sample_valid(al_store_first()));
  TEST_ASSERT_FALSE(al_sample_valid(al_store_last()));
  TEST_ASSERT_FALSE(al_sample_valid(al_store_get(AL_STORE_SHORT, 0)));
  TEST_ASSERT_FALSE(al_sample_valid(al_store_get(AL_STORE_SHORT, -1)));

  // verify empty source
  al_sample_source_t src = al_store_source();
  TEST_ASSERT_EQUAL_size_t(0, src.info(src.ctx).count);
}

static void test_store_ingest() {
  test_store_reset();

  // ingest three samples
  store_fill(3, 10000);
  TEST_ASSERT_EQUAL_size_t(3, al_store_count(AL_STORE_SHORT));
  TEST_ASSERT_EQUAL_size_t(0, al_store_count(AL_STORE_LONG));

  // verify positive and negative indexing
  TEST_ASSERT_EQUAL_INT32(10000, al_store_get(AL_STORE_SHORT, 0).off);
  TEST_ASSERT_EQUAL_INT32(15000, al_store_get(AL_STORE_SHORT, 1).off);
  TEST_ASSERT_EQUAL_INT32(20000, al_store_get(AL_STORE_SHORT, 2).off);
  TEST_ASSERT_EQUAL_INT32(20000, al_store_get(AL_STORE_SHORT, -1).off);
  TEST_ASSERT_EQUAL_INT32(10000, al_store_get(AL_STORE_SHORT, -3).off);

  // verify indexes are clamped at both ends
  TEST_ASSERT_EQUAL_INT32(20000, al_store_get(AL_STORE_SHORT, 5).off);
  TEST_ASSERT_EQUAL_INT32(10000, al_store_get(AL_STORE_SHORT, -5).off);

  // verify first/last with an empty long store
  TEST_ASSERT_EQUAL_INT32(10000, al_store_first().off);
  TEST_ASSERT_EQUAL_INT32(20000, al_store_last().off);
}

static void test_store_base() {
  test_store_reset();

  // fill both stores (181st ingest moves the first sample to the long store)
  store_fill(181, 0);
  TEST_ASSERT_EQUAL_size_t(1, al_store_count(AL_STORE_LONG));

  // verify setting the base without moving keeps offsets
  TEST_ASSERT_EQUAL_INT64(0, al_store_get_base());
  al_store_set_base(1000, false);
  TEST_ASSERT_EQUAL_INT64(1000, al_store_get_base());
  TEST_ASSERT_EQUAL_INT32(0, al_store_get(AL_STORE_LONG, 0).off);
  TEST_ASSERT_EQUAL_INT32(5000, al_store_get(AL_STORE_SHORT, 0).off);

  // verify setting the base with moving shifts both stores
  al_store_set_base(6000, true);
  TEST_ASSERT_EQUAL_INT64(6000, al_store_get_base());
  TEST_ASSERT_EQUAL_INT32(-5000, al_store_get(AL_STORE_LONG, 0).off);
  TEST_ASSERT_EQUAL_INT32(0, al_store_get(AL_STORE_SHORT, 0).off);
  TEST_ASSERT_EQUAL_INT32(895000, al_store_get(AL_STORE_SHORT, -1).off);
}

static void test_store_move() {
  test_store_reset();

  // fill until the second move: the 181st ingest moves the first sample as the
  // long store is empty, then samples are dropped until one is a full interval
  // (60s) newer than the last long sample (sample 13 at 65s, on ingest 194)
  store_fill(194, 0);
  TEST_ASSERT_EQUAL_size_t(2, al_store_count(AL_STORE_LONG));
  TEST_ASSERT_EQUAL_size_t(180, al_store_count(AL_STORE_SHORT));
  TEST_ASSERT_EQUAL_INT32(0, al_store_get(AL_STORE_LONG, 0).off);
  TEST_ASSERT_EQUAL_INT16(400, al_store_get(AL_STORE_LONG, 0).co2);
  TEST_ASSERT_EQUAL_INT32(65000, al_store_get(AL_STORE_LONG, 1).off);
  TEST_ASSERT_EQUAL_INT16(413, al_store_get(AL_STORE_LONG, 1).co2);

  // verify short store wrapped and holds the newest 180 samples
  TEST_ASSERT_EQUAL_INT32(70000, al_store_get(AL_STORE_SHORT, 0).off);
  TEST_ASSERT_EQUAL_INT16(414, al_store_get(AL_STORE_SHORT, 0).co2);
  TEST_ASSERT_EQUAL_INT32(965000, al_store_get(AL_STORE_SHORT, -1).off);
  TEST_ASSERT_EQUAL_INT16(593, al_store_get(AL_STORE_SHORT, -1).co2);

  // verify first/last span both stores
  TEST_ASSERT_EQUAL_INT32(0, al_store_first().off);
  TEST_ASSERT_EQUAL_INT32(965000, al_store_last().off);

  // verify a shorter interval moves samples earlier (sample 7 at 35s)
  test_store_reset();
  al_store_set_interval(30);
  store_fill(188, 0);
  TEST_ASSERT_EQUAL_size_t(2, al_store_count(AL_STORE_LONG));
  TEST_ASSERT_EQUAL_INT32(35000, al_store_get(AL_STORE_LONG, 1).off);
  TEST_ASSERT_EQUAL_INT32(40000, al_store_get(AL_STORE_SHORT, 0).off);
}

static void test_store_source() {
  test_store_reset();

  // fill both stores with a non-zero first offset and base
  store_fill(181, 10000);
  al_store_set_base(500, false);
  al_sample_source_t src = al_store_source();

  // verify info snapshot
  al_sample_info_t info = src.info(src.ctx);
  TEST_ASSERT_EQUAL_size_t(181, info.count);
  TEST_ASSERT_EQUAL_INT64(10500, info.start);
  TEST_ASSERT_EQUAL_INT32(900000, info.length);

  // verify reads across the store boundary are rebased to the first sample
  al_sample_t samples[3];
  src.read(src.ctx, samples, 3, 0);
  TEST_ASSERT_EQUAL_INT32(0, samples[0].off);
  TEST_ASSERT_EQUAL_INT16(400, samples[0].co2);
  TEST_ASSERT_EQUAL_INT32(5000, samples[1].off);
  TEST_ASSERT_EQUAL_INT16(401, samples[1].co2);
  TEST_ASSERT_EQUAL_INT32(10000, samples[2].off);
  TEST_ASSERT_EQUAL_INT16(402, samples[2].co2);
  src.read(src.ctx, samples, 2, 1);
  TEST_ASSERT_EQUAL_INT16(401, samples[0].co2);
  TEST_ASSERT_EQUAL_INT16(402, samples[1].co2);

  // verify negative offsets read from the end of the combined source
  src.read(src.ctx, samples, 1, (size_t)-1);
  TEST_ASSERT_EQUAL_INT32(900000, samples[0].off);
  TEST_ASSERT_EQUAL_INT16(580, samples[0].co2);
  src.read(src.ctx, samples, 2, (size_t)-2);
  TEST_ASSERT_EQUAL_INT16(579, samples[0].co2);
  TEST_ASSERT_EQUAL_INT16(580, samples[1].co2);

  // verify picking from the end works through the source
  float values[2] = {0};
  float min = 0, max = 0;
  TEST_ASSERT_EQUAL_size_t(2, al_sample_pick(&src, AL_SAMPLE_CO2, -2, values + 2, &min, &max));
  TEST_ASSERT_EQUAL_FLOAT(579.f, values[0]);
  TEST_ASSERT_EQUAL_FLOAT(580.f, values[1]);
  TEST_ASSERT_EQUAL_FLOAT(579.f, min);
  TEST_ASSERT_EQUAL_FLOAT(580.f, max);

  // verify negative offsets with an empty long store
  test_store_reset();
  store_fill(3, 0);
  src.read(src.ctx, samples, 1, (size_t)-1);
  TEST_ASSERT_EQUAL_INT32(10000, samples[0].off);
  TEST_ASSERT_EQUAL_INT16(402, samples[0].co2);
}

void suite_store() {
  al_store_init();
  RUN_TEST(test_store_interval);
  RUN_TEST(test_store_empty);
  RUN_TEST(test_store_ingest);
  RUN_TEST(test_store_base);
  RUN_TEST(test_store_move);
  RUN_TEST(test_store_source);
}
