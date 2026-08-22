#include <unity.h>

// include the unit directly to access its static state for resets
#include "../src/dat.c"

/* Helpers */

void test_store_reset();

static void dat_test_reset() {
  // wipe scratch filesystem and reset unit state
  al_storage_reset();
  dat_files_length = 0;
  dat_counter = 0;
  al_storage_test_free[AL_STORAGE_INT] = 4 * 1024 * 1024;
  al_storage_test_free[AL_STORAGE_EXT] = 4 * 1024 * 1024;
}

static int prog_calls = 0;
static size_t prog_current = 0;
static size_t prog_total = 0;

static void prog_record(size_t current, size_t total) {
  prog_calls++;
  prog_current = current;
  prog_total = total;
}

/* Tests */

static void test_dat_create() {
  dat_test_reset();

  // verify creation advances the counter and writes the head
  TEST_ASSERT_EQUAL_UINT16(1, dat_next());
  TEST_ASSERT_EQUAL_UINT16(1, dat_create(1700000000000LL));
  TEST_ASSERT_EQUAL_UINT16(2, dat_next());
  TEST_ASSERT_EQUAL_size_t(1, dat_count());
  TEST_ASSERT_EQUAL_INT(sizeof(dat_head_t), al_storage_stat(AL_STORAGE_INT, "data", "file-0001.bin"));

  // verify file state
  dat_file_t *file = dat_find(1, NULL);
  TEST_ASSERT_NOT_NULL(file);
  TEST_ASSERT_EQUAL_UINT32(DAT_MAGIC, file->head.magic);
  TEST_ASSERT_EQUAL_UINT16(DAT_VERSION, file->head.version);
  TEST_ASSERT_EQUAL_INT64(1700000000000LL, file->head.start);
  TEST_ASSERT_EQUAL_size_t(0, file->size);

  // verify find by number and index
  TEST_ASSERT_EQUAL_UINT16(2, dat_create(1700000100000LL));
  int index = -1;
  TEST_ASSERT_NOT_NULL(dat_find(2, &index));
  TEST_ASSERT_EQUAL_INT(1, index);
  TEST_ASSERT_NULL(dat_find(3, NULL));
  TEST_ASSERT_EQUAL_PTR(dat_get(0), dat_find(1, NULL));
}

static void test_dat_append_load() {
  dat_test_reset();

  // append samples
  uint16_t num = dat_create(1700000000000LL);
  al_sample_t samples[3] = {
      {.off = 0, .co2 = 400},
      {.off = 5000, .co2 = 401},
      {.off = 10000, .co2 = 402},
  };
  dat_append(num, samples, 3);

  // verify file state and disk size
  dat_file_t *file = dat_find(num, NULL);
  TEST_ASSERT_EQUAL_size_t(3, file->size);
  TEST_ASSERT_EQUAL_INT32(10000, file->stop);
  TEST_ASSERT_EQUAL_INT(sizeof(dat_head_t) + 3 * sizeof(al_sample_t),
                        al_storage_stat(AL_STORAGE_INT, "data", "file-0001.bin"));

  // verify incremental append
  al_sample_t more[2] = {
      {.off = 15000, .co2 = 403},
      {.off = 20000, .co2 = 404},
  };
  dat_append(num, more, 2);
  TEST_ASSERT_EQUAL_size_t(5, file->size);
  TEST_ASSERT_EQUAL_INT32(20000, file->stop);

  // verify full and partial loads
  al_sample_t out[5] = {0};
  dat_load(num, out, 5, 0);
  TEST_ASSERT_EQUAL_INT32(0, out[0].off);
  TEST_ASSERT_EQUAL_INT16(400, out[0].co2);
  TEST_ASSERT_EQUAL_INT16(404, out[4].co2);
  dat_load(num, out, 2, 3);
  TEST_ASSERT_EQUAL_INT16(403, out[0].co2);
  TEST_ASSERT_EQUAL_INT16(404, out[1].co2);
}

static void test_dat_mark() {
  dat_test_reset();

  // add a mark
  uint16_t num = dat_create(1700000000000LL);
  dat_mark(num, 60000);
  dat_file_t *file = dat_find(num, NULL);
  TEST_ASSERT_EQUAL_INT8(1, file->marks);
  TEST_ASSERT_EQUAL_INT32(60000, file->head.marks[0]);

  // verify marks are capped at the limit
  for (int i = 2; i <= 105; i++) {
    dat_mark(num, i * 1000);
  }
  TEST_ASSERT_EQUAL_INT8(DAT_MARKS, file->marks);
  TEST_ASSERT_EQUAL_INT32(99000, file->head.marks[DAT_MARKS - 1]);
}

static void test_dat_scan() {
  dat_test_reset();

  // create a file with samples, a mark and a partial trailing sample
  uint16_t num1 = dat_create(1700000000000LL);
  al_sample_t samples[3] = {
      {.off = 0, .co2 = 400},
      {.off = 5000, .co2 = 401},
      {.off = 10000, .co2 = 402},
  };
  dat_append(num1, samples, 3);
  dat_mark(num1, 7000);
  uint8_t junk[9] = {0xAA};
  al_storage_write(AL_STORAGE_INT, "data", "file-0001.bin", junk, sizeof(dat_head_t) + 3 * sizeof(al_sample_t),
                   sizeof(junk), false);

  // create an empty file
  uint16_t num2 = dat_create(1800000000000LL);

  // write undersized, foreign magic and foreign version files
  uint32_t magic = DAT_MAGIC;
  al_storage_write(AL_STORAGE_INT, "data", "tiny.bin", &magic, 0, sizeof(magic), true);
  dat_head_t foreign = {.magic = 0x12345678, .version = DAT_VERSION};
  al_storage_write(AL_STORAGE_INT, "data", "magic.bin", &foreign, 0, sizeof(foreign), true);
  foreign = (dat_head_t){.magic = DAT_MAGIC, .version = 99};
  al_storage_write(AL_STORAGE_INT, "data", "version.bin", &foreign, 0, sizeof(foreign), true);

  // rescan directory as done after a reboot
  dat_files_length = 0;
  dat_init();

  // verify the two files were restored and the others skipped
  TEST_ASSERT_EQUAL_size_t(2, dat_count());
  dat_file_t *file = dat_find(num1, NULL);
  TEST_ASSERT_NOT_NULL(file);
  TEST_ASSERT_EQUAL_INT64(1700000000000LL, file->head.start);
  TEST_ASSERT_EQUAL_size_t(3, file->size);
  TEST_ASSERT_EQUAL_INT32(10000, file->stop);
  TEST_ASSERT_EQUAL_INT8(1, file->marks);
  TEST_ASSERT_EQUAL_INT32(7000, file->head.marks[0]);
  file = dat_find(num2, NULL);
  TEST_ASSERT_NOT_NULL(file);
  TEST_ASSERT_EQUAL_size_t(0, file->size);
  TEST_ASSERT_EQUAL_INT32(0, file->stop);
  TEST_ASSERT_EQUAL_INT8(0, file->marks);
}

static void test_dat_delete() {
  dat_test_reset();

  // create three files and delete the middle one
  dat_create(1000);
  dat_create(2000);
  dat_create(3000);
  dat_delete(2);

  // verify the list is compacted and the file removed from disk
  TEST_ASSERT_EQUAL_size_t(2, dat_count());
  TEST_ASSERT_NULL(dat_find(2, NULL));
  TEST_ASSERT_EQUAL_UINT16(1, dat_get(0)->head.num);
  TEST_ASSERT_EQUAL_UINT16(3, dat_get(1)->head.num);
  TEST_ASSERT_EQUAL_INT(-1, al_storage_stat(AL_STORAGE_INT, "data", "file-0002.bin"));
  TEST_ASSERT_EQUAL_INT(sizeof(dat_head_t), al_storage_stat(AL_STORAGE_INT, "data", "file-0003.bin"));
}

static void test_dat_source() {
  dat_test_reset();

  // create a file with samples
  uint16_t num = dat_create(1700000000000LL);
  al_sample_t samples[3] = {
      {.off = 0, .co2 = 400},
      {.off = 5000, .co2 = 401},
      {.off = 10000, .co2 = 402},
  };
  dat_append(num, samples, 3);

  // verify source count, start, stop and reads
  al_sample_source_t src = dat_source(num);
  TEST_ASSERT_EQUAL_size_t(3, src.count(src.ctx));
  TEST_ASSERT_EQUAL_INT64(1700000000000LL, src.start(src.ctx));
  TEST_ASSERT_EQUAL_INT32(10000, src.stop(src.ctx));
  al_sample_t out[2];
  src.read(src.ctx, out, 2, 1);
  TEST_ASSERT_EQUAL_INT32(5000, out[0].off);
  TEST_ASSERT_EQUAL_INT16(401, out[0].co2);
  TEST_ASSERT_EQUAL_INT16(402, out[1].co2);
}

static void test_dat_import() {
  dat_test_reset();

  // populate the sample store
  test_store_reset();
  al_store_set_base(1700000000000LL, false);
  for (int i = 0; i < 40; i++) {
    al_store_ingest((al_sample_t){.off = i * 5000, .co2 = (int16_t)(400 + i)});
  }

  // verify import fails on low space without appending
  uint16_t num = dat_create(1700000000000LL);
  al_storage_test_free[AL_STORAGE_INT] = 1000;
  TEST_ASSERT_FALSE(dat_import(num, 0, NULL));
  TEST_ASSERT_EQUAL_size_t(0, dat_find(num, NULL)->size);
  al_storage_test_free[AL_STORAGE_INT] = 4 * 1024 * 1024;

  // import all samples
  prog_calls = 0;
  TEST_ASSERT_TRUE(dat_import(num, 0, prog_record));
  dat_file_t *file = dat_find(num, NULL);
  TEST_ASSERT_EQUAL_size_t(40, file->size);
  TEST_ASSERT_EQUAL_INT32(195000, file->stop);
  al_sample_t out[1];
  dat_load(num, out, 1, 0);
  TEST_ASSERT_EQUAL_INT32(0, out[0].off);
  TEST_ASSERT_EQUAL_INT16(400, out[0].co2);
  dat_load(num, out, 1, 39);
  TEST_ASSERT_EQUAL_INT32(195000, out[0].off);
  TEST_ASSERT_EQUAL_INT16(439, out[0].co2);

  // verify progress calls: initial plus one per batch of 32
  TEST_ASSERT_EQUAL_INT(3, prog_calls);
  TEST_ASSERT_EQUAL_size_t(40, prog_current);
  TEST_ASSERT_EQUAL_size_t(40, prog_total);

  // verify resuming from the end appends nothing
  TEST_ASSERT_TRUE(dat_import(num, 40, NULL));
  TEST_ASSERT_EQUAL_size_t(40, file->size);

  // verify offsets are adjusted when the file starts before the samples
  uint16_t num2 = dat_create(1700000000000LL - 3000);
  TEST_ASSERT_TRUE(dat_import(num2, 0, NULL));
  file = dat_find(num2, NULL);
  TEST_ASSERT_EQUAL_size_t(40, file->size);
  dat_load(num2, out, 1, 0);
  TEST_ASSERT_EQUAL_INT32(3000, out[0].off);
  TEST_ASSERT_EQUAL_INT32(198000, file->stop);
}

static void test_dat_export() {
  dat_test_reset();
  al_sample_set_altitude(0.f);

  // append enough samples to trigger buffer flushes, with gaps in the
  // voc/nox/pm coverage, and build the expected CSV alongside
  uint16_t num = dat_create(1700000000000LL);
  static char expected[16384];
  size_t pos = 0;
  pos += snprintf(expected + pos, sizeof(expected) - pos, "time,co2,tmp,hum,voc,nox,prs,pm\n");
  for (int i = 0; i < 200; i++) {
    al_sample_t sample = {
        .off = i * 5000,
        .co2 = (int16_t)(400 + i),
        .tmp = (int16_t)(2000 + i),
        .hum = 5000,
        .voc = (int16_t)(i % 3 == 0 ? 0 : 100 + i),
        .nox = (int16_t)(i % 2 == 0 ? 50 : 0),
        .prs = 950,
        .pm = (int16_t)(i % 4 == 0 ? -1 : 100 + i),
    };
    dat_append(num, &sample, 1);
    char voc_str[8] = {0}, nox_str[8] = {0}, pm_str[8] = {0};
    if (sample.voc != 0) snprintf(voc_str, sizeof(voc_str), "%d", sample.voc);
    if (sample.nox != 0) snprintf(nox_str, sizeof(nox_str), "%d", sample.nox);
    if (sample.pm >= 0) snprintf(pm_str, sizeof(pm_str), "%.1f", (double)((float)sample.pm / 10.f));
    pos += snprintf(expected + pos, sizeof(expected) - pos, "%lld,%d,%.2f,%.2f,%s,%s,%d,%s\n",
                    (long long)(1700000000000LL + sample.off), sample.co2, (double)((float)sample.tmp / 100.f),
                    (double)((float)sample.hum / 100.f), voc_str, nox_str, sample.prs, pm_str);
  }

  // export and compare content
  TEST_ASSERT_TRUE(dat_export(num, NULL));
  TEST_ASSERT_EQUAL_INT((int)pos, al_storage_stat(AL_STORAGE_EXT, "export", "file-0001.csv"));
  static char content[16384];
  TEST_ASSERT_TRUE(al_storage_read(AL_STORAGE_EXT, "export", "file-0001.csv", content, 0, pos));
  TEST_ASSERT_EQUAL_MEMORY(expected, content, pos);

  // verify re-export truncates instead of appending
  TEST_ASSERT_TRUE(dat_export(num, NULL));
  TEST_ASSERT_EQUAL_INT((int)pos, al_storage_stat(AL_STORAGE_EXT, "export", "file-0001.csv"));

  // verify export fails on low space
  al_storage_test_free[AL_STORAGE_EXT] = 100;
  TEST_ASSERT_FALSE(dat_export(num, NULL));
}

static void test_dat_capacity() {
  dat_test_reset();

  // fill the file list to capacity
  for (int i = 0; i < DAT_FILES; i++) {
    TEST_ASSERT_NOT_EQUAL(0, dat_create(1000 + i));
  }
  TEST_ASSERT_EQUAL_size_t(DAT_FILES, dat_count());

  // verify creation on a full list errors without overflowing
  *esp_err_expected() = 1;
  TEST_ASSERT_EQUAL_UINT16(0, dat_create(9999));
  TEST_ASSERT_EQUAL_INT(0, *esp_err_expected());
  TEST_ASSERT_EQUAL_size_t(DAT_FILES, dat_count());
}

void suite_dat() {
  // start with a clean scratch filesystem
  al_storage_reset();
  dat_init();
  RUN_TEST(test_dat_create);
  RUN_TEST(test_dat_append_load);
  RUN_TEST(test_dat_mark);
  RUN_TEST(test_dat_scan);
  RUN_TEST(test_dat_delete);
  RUN_TEST(test_dat_source);
  RUN_TEST(test_dat_import);
  RUN_TEST(test_dat_export);
  RUN_TEST(test_dat_capacity);
}
