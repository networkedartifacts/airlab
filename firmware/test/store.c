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

// non-static, as other suites use them to access the internal base
void test_store_seed_base(int64_t base) { al_store_base = base; }
int64_t test_store_get_base() { return al_store_base; }

#define STORE_BASE 1600000000000LL

// resolve a stored sample to its epoch time, robust against internal rebasing
static int64_t store_epoch(al_store_t store, int num) { return al_store_base + al_store_get(store, num).off; }

static void store_fill(int num, int64_t start) {
  // ingest samples 5s apart with increasing CO2 values
  for (int i = 0; i < num; i++) {
    al_store_ingest(start + i * 5000, (al_sample_t){.co2 = (int16_t)(400 + i)});
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
  store_fill(3, STORE_BASE + 10000);
  TEST_ASSERT_EQUAL_size_t(3, al_store_count(AL_STORE_SHORT));
  TEST_ASSERT_EQUAL_size_t(0, al_store_count(AL_STORE_LONG));

  // verify positive and negative indexing
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 10000, store_epoch(AL_STORE_SHORT, 0));
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 15000, store_epoch(AL_STORE_SHORT, 1));
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 20000, store_epoch(AL_STORE_SHORT, 2));
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 20000, store_epoch(AL_STORE_SHORT, -1));
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 10000, store_epoch(AL_STORE_SHORT, -3));

  // verify indexes are clamped at both ends
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 20000, store_epoch(AL_STORE_SHORT, 5));
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 10000, store_epoch(AL_STORE_SHORT, -5));

  // verify first/last with an empty long store
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 10000, al_store_base + al_store_first().off);
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 20000, al_store_base + al_store_last().off);
}

static void test_store_base() {
  test_store_reset();

  // verify the base is claimed by the first ingest
  TEST_ASSERT_EQUAL_INT64(0, al_store_base);
  al_store_ingest(STORE_BASE, (al_sample_t){.co2 = 400});
  TEST_ASSERT_EQUAL_INT64(STORE_BASE, al_store_base);
  TEST_ASSERT_EQUAL_INT32(0, al_store_last().off);

  // verify ingests within the rebase interval keep the base
  al_store_ingest(STORE_BASE + 60000, (al_sample_t){.co2 = 401});
  TEST_ASSERT_EQUAL_INT64(STORE_BASE, al_store_base);
  TEST_ASSERT_EQUAL_INT32(60000, al_store_last().off);

  // verify an ingest beyond the rebase interval rebases and shifts the offsets
  al_store_ingest(STORE_BASE + 300000, (al_sample_t){.co2 = 402});
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 300000, al_store_base);
  TEST_ASSERT_EQUAL_INT32(-300000, al_store_get(AL_STORE_SHORT, 0).off);
  TEST_ASSERT_EQUAL_INT32(0, al_store_last().off);
  TEST_ASSERT_EQUAL_INT64(STORE_BASE, store_epoch(AL_STORE_SHORT, 0));

  // verify shifting moves the samples in time without touching offsets
  al_store_shift(5000);
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 305000, al_store_base);
  TEST_ASSERT_EQUAL_INT32(-300000, al_store_get(AL_STORE_SHORT, 0).off);
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 5000, store_epoch(AL_STORE_SHORT, 0));

  // verify a shift without a claimed base is ignored
  test_store_reset();
  al_store_shift(5000);
  TEST_ASSERT_EQUAL_INT64(0, al_store_base);
}

static void test_store_move() {
  test_store_reset();

  // fill until the second move: the 181st ingest moves the first sample as the
  // long store is empty, then samples are dropped until one is a full interval
  // (60s) newer than the last long sample (sample 13 at 65s, on ingest 194)
  store_fill(194, STORE_BASE);
  TEST_ASSERT_EQUAL_size_t(2, al_store_count(AL_STORE_LONG));
  TEST_ASSERT_EQUAL_size_t(180, al_store_count(AL_STORE_SHORT));
  TEST_ASSERT_EQUAL_INT64(STORE_BASE, store_epoch(AL_STORE_LONG, 0));
  TEST_ASSERT_EQUAL_INT16(400, al_store_get(AL_STORE_LONG, 0).co2);
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 65000, store_epoch(AL_STORE_LONG, 1));
  TEST_ASSERT_EQUAL_INT16(413, al_store_get(AL_STORE_LONG, 1).co2);

  // verify short store wrapped and holds the newest 180 samples
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 70000, store_epoch(AL_STORE_SHORT, 0));
  TEST_ASSERT_EQUAL_INT16(414, al_store_get(AL_STORE_SHORT, 0).co2);
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 965000, store_epoch(AL_STORE_SHORT, -1));
  TEST_ASSERT_EQUAL_INT16(593, al_store_get(AL_STORE_SHORT, -1).co2);

  // verify first/last span both stores
  TEST_ASSERT_EQUAL_INT64(STORE_BASE, al_store_base + al_store_first().off);
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 965000, al_store_base + al_store_last().off);

  // verify a shorter interval moves samples earlier (sample 7 at 35s)
  test_store_reset();
  al_store_set_interval(30);
  store_fill(188, STORE_BASE);
  TEST_ASSERT_EQUAL_size_t(2, al_store_count(AL_STORE_LONG));
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 35000, store_epoch(AL_STORE_LONG, 1));
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 40000, store_epoch(AL_STORE_SHORT, 0));
}

static void test_store_source() {
  test_store_reset();

  // fill both stores with a non-zero first offset (rebases internally)
  store_fill(181, STORE_BASE + 10000);
  al_sample_source_t src = al_store_source();

  // verify info snapshot
  al_sample_info_t info = src.info(src.ctx);
  TEST_ASSERT_EQUAL_size_t(181, info.count);
  TEST_ASSERT_EQUAL_INT64(STORE_BASE + 10000, info.start);
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
  store_fill(3, STORE_BASE);
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
