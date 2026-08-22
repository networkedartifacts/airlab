#include <unity.h>

// include the unit directly to access its static state for resets
#include "../src/rec.c"

/* Fakes */

naos_task_t naos_run(const char *name, uint16_t stack, int core, naos_func_t func) {
  // never run the recording task, tests exercise the functions directly
  (void)name;
  (void)stack;
  (void)core;
  (void)func;
  return NULL;
}

/* Helpers */

void test_store_reset();
void test_dat_reset();
void test_clock_set_epoch(int64_t epoch);

#define REC_BASE 1700000000000LL

static void rec_test_reset() {
  test_dat_reset();
  test_store_reset();
  rec_current = 0;
}

/* Tests */

static void test_rec_free() {
  rec_test_reset();

  // verify free samples with the new and continue reserves subtracted
  al_storage_test_free[AL_STORAGE_INT] = 100 * 1024;
  TEST_ASSERT_EQUAL_UINT32((100 * 1024 - REC_MIN_FREE_NEW) / sizeof(al_sample_t), rec_free(true));
  TEST_ASSERT_EQUAL_UINT32((100 * 1024 - REC_MIN_FREE_CONT) / sizeof(al_sample_t), rec_free(false));

  // verify zero below and at the reserves
  al_storage_test_free[AL_STORAGE_INT] = REC_MIN_FREE_NEW - 1;
  TEST_ASSERT_EQUAL_UINT32(0, rec_free(true));
  TEST_ASSERT_EQUAL_UINT32((REC_MIN_FREE_NEW - 1 - REC_MIN_FREE_CONT) / sizeof(al_sample_t), rec_free(false));
  al_storage_test_free[AL_STORAGE_INT] = REC_MIN_FREE_CONT - 1;
  TEST_ASSERT_EQUAL_UINT32(0, rec_free(false));
  al_storage_test_free[AL_STORAGE_INT] = REC_MIN_FREE_NEW;
  TEST_ASSERT_EQUAL_UINT32(0, rec_free(true));
}

static void test_rec_start_stop() {
  rec_test_reset();
  rec_init(true);

  // verify initial state
  TEST_ASSERT_FALSE(rec_running());
  TEST_ASSERT_EQUAL_UINT16(0, rec_file());

  // verify starting and stopping a recording
  uint16_t num = dat_create(REC_BASE);
  rec_start(num);
  TEST_ASSERT_TRUE(rec_running());
  TEST_ASSERT_EQUAL_UINT16(num, rec_file());
  rec_stop();
  TEST_ASSERT_FALSE(rec_running());
  TEST_ASSERT_EQUAL_UINT16(0, rec_file());

  // verify stopping while not running errors
  *esp_err_expected() = 1;
  rec_stop();
  TEST_ASSERT_EQUAL_INT(0, *esp_err_expected());

  // verify starting while running errors
  rec_start(num);
  *esp_err_expected() = 1;
  rec_start(num);
  TEST_ASSERT_EQUAL_INT(0, *esp_err_expected());
  rec_stop();

  // verify starting without free space errors
  al_storage_test_free[AL_STORAGE_INT] = REC_MIN_FREE_CONT - 1;
  *esp_err_expected() = 1;
  rec_start(num);
  TEST_ASSERT_EQUAL_INT(0, *esp_err_expected());
}

static void test_rec_mark() {
  rec_test_reset();
  rec_init(true);

  // verify marking while not running errors
  *esp_err_expected() = 1;
  rec_mark();
  TEST_ASSERT_EQUAL_INT(0, *esp_err_expected());

  // verify marks are placed relative to the file start
  uint16_t num = dat_create(REC_BASE);
  rec_start(num);
  test_clock_set_epoch(REC_BASE + 65000);
  rec_mark();
  dat_file_t *file = dat_find(num, NULL);
  TEST_ASSERT_EQUAL_INT8(1, file->marks);
  TEST_ASSERT_EQUAL_INT32(65000, file->head.marks[0]);
  rec_stop();
}

static void test_rec_backfill() {
  rec_test_reset();

  // populate the store with 20 samples
  al_store_set_base(REC_BASE, false);
  for (int i = 0; i < 20; i++) {
    al_store_ingest((al_sample_t){.off = i * 5000, .co2 = (int16_t)(400 + i)});
  }

  // create a file holding the first 10 samples, as before a reboot
  uint16_t num = dat_create(REC_BASE);
  for (int i = 0; i < 10; i++) {
    al_sample_t sample = {.off = i * 5000, .co2 = (int16_t)(400 + i)};
    dat_append(num, &sample, 1);
  }

  // verify init resumes the recording with the missed samples
  rec_current = num;
  rec_init(false);
  dat_file_t *file = dat_find(num, NULL);
  TEST_ASSERT_EQUAL_size_t(20, file->size);
  TEST_ASSERT_EQUAL_INT32(95000, file->stop);
  al_sample_t out[1];
  dat_load(num, out, 1, 10);
  TEST_ASSERT_EQUAL_INT32(50000, out[0].off);
  TEST_ASSERT_EQUAL_INT16(410, out[0].co2);

  // verify an up-to-date recording is left alone
  rec_init(false);
  TEST_ASSERT_EQUAL_size_t(20, file->size);

  // verify a reset init skips the backfill
  al_store_ingest((al_sample_t){.off = 100000, .co2 = 420});
  rec_init(true);
  TEST_ASSERT_EQUAL_size_t(20, file->size);

  // verify a missing file errors
  rec_current = 999;
  *esp_err_expected() = 1;
  rec_init(false);
  TEST_ASSERT_EQUAL_INT(0, *esp_err_expected());
}

static void test_rec_backfill_shifted() {
  rec_test_reset();

  // populate the store with 5 samples
  al_store_set_base(REC_BASE, false);
  for (int i = 0; i < 5; i++) {
    al_store_ingest((al_sample_t){.off = i * 5000, .co2 = (int16_t)(400 + i)});
  }

  // verify backfill into an empty file that starts before the samples
  uint16_t num = dat_create(REC_BASE - 3000);
  rec_current = num;
  rec_init(false);
  dat_file_t *file = dat_find(num, NULL);
  TEST_ASSERT_EQUAL_size_t(5, file->size);
  TEST_ASSERT_EQUAL_INT32(23000, file->stop);
  al_sample_t out[1];
  dat_load(num, out, 1, 0);
  TEST_ASSERT_EQUAL_INT32(3000, out[0].off);
  TEST_ASSERT_EQUAL_INT16(400, out[0].co2);
}

void suite_rec() {
  RUN_TEST(test_rec_free);
  RUN_TEST(test_rec_start_stop);
  RUN_TEST(test_rec_mark);
  RUN_TEST(test_rec_backfill);
  RUN_TEST(test_rec_backfill_shifted);
}
