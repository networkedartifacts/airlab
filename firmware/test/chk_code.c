#include <math.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "chk_code.h"

// The reference vectors are the exact bytes the page at
// https://airlab.today/ac/ decodes. Agreeing with that page byte for byte is
// the only thing that makes a scanned link readable, so these are checked as
// exact strings rather than as properties: a change here that does not also
// change the page breaks every code this firmware draws.

static const char* VENT_DIGITS =
    "31112749379027852418000570666471608232269532654405854977750303453368147075447037245069366838930136166603"
    "9697512";

static const char* STOVE_DIGITS =
    "20889143241590655319545191127851952922185282863603337230964716166795751136151598730562340742913543160532"
    "54316342520280726820136277479308660602255410395443946178642055131719799130671014023166780208856877220573"
    "7193565641504905780031862848146125808658033372236627072058347484235116801368184";

// the same header the reference was given: minute 1000000 with the room two
// hours east of UTC, which the field carries as 56 quarter hours from -12:00,
// on the device named AL5AB12C
#define TEST_META(id) \
  { .check = (id), .minute = 1000000, .offset = 120, .device = 0x5AB12C, .room = 0, .cadence = 2 }

static void vent_samples(float* out, size_t n) {
  for (size_t i = 0; i < n; i++) {
    double t = i * 5.0 / 3600.0;
    out[i] = (float)floor(425 + (843 - 425) * exp(-2.9 * t) + 0.5);
  }
}

static void test_a_ventilation_payload_matches_the_page() {
  float samples[72];
  vent_samples(samples, 72);

  // ach, achSe, r2, c0, c1, cout, pre
  const float fields[] = {2.9f, 0.15f, 0.99f, 843, samples[71], 425, 6};
  const chk_code_meta_t meta = TEST_META(CHK_CODE_VENT);

  char digits[CHK_CODE_MAX_DIGITS];
  int step = 0;
  size_t bytes = 0;
  TEST_ASSERT_TRUE(chk_code_pack(&meta, fields, 7, samples, 72, 153, digits, sizeof(digits), &step, &bytes));

  TEST_ASSERT_EQUAL_STRING(VENT_DIGITS, digits);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, step, "a short check keeps the finest step");
  TEST_ASSERT_EQUAL_size_t(46, bytes);
}

static void test_a_stove_payload_matches_the_page() {
  float samples[120];
  for (size_t i = 0; i < 120; i++) {
    samples[i] = (float)(600 + (int)floor(200 * sin(i / 10.0) + 0.5) + (int)i * 3);
  }

  // ce, hoodAch, slope1, slope3, c0, noxPeak, pre, pass1, pass2, pass3
  const float fields[] = {62, 4.1f, 200, 76, 612, 46, 12, 24, 40, 24};
  const chk_code_meta_t meta = TEST_META(CHK_CODE_STOVE);

  char digits[CHK_CODE_MAX_DIGITS];
  int step = 0;
  size_t bytes = 0;
  TEST_ASSERT_TRUE(chk_code_pack(&meta, fields, 10, samples, 120, 153, digits, sizeof(digits), &step, &bytes));

  TEST_ASSERT_EQUAL_STRING(STOVE_DIGITS, digits);
  TEST_ASSERT_EQUAL_size_t(119, bytes);
}

static void test_a_long_check_loses_resolution_rather_than_failing() {
  // the encoder walks the steps from fine to coarse and keeps the first that
  // fits, so a check too long for the finest step still produces a link
  float samples[400];
  for (size_t i = 0; i < 400; i++) {
    samples[i] = (float)(500 + (i * 37) % 900);  // deliberately noisy, so deltas are large
  }
  const float fields[] = {2.9f, 0.15f, 0.99f, 843, 500, 425, 6};
  const chk_code_meta_t meta = TEST_META(CHK_CODE_VENT);

  char digits[CHK_CODE_MAX_DIGITS];
  int step = 0;
  size_t bytes = 0;
  TEST_ASSERT_TRUE(chk_code_pack(&meta, fields, 7, samples, 400, 153, digits, sizeof(digits), &step, &bytes));

  TEST_ASSERT_TRUE_MESSAGE(step > 1, "a long noisy check must have been coarsened");
  TEST_ASSERT_TRUE_MESSAGE(bytes <= 153, "and must fit the budget it was given");
}

static void test_an_unknown_check_is_refused() {
  float samples[4] = {1, 2, 3, 4};
  const float fields[] = {0, 0, 0, 0, 0, 0, 0};
  char digits[CHK_CODE_MAX_DIGITS];

  // zero is no check, and the purifier has no layout in this firmware yet
  chk_code_meta_t meta = {.check = 0};
  TEST_ASSERT_FALSE(chk_code_pack(&meta, fields, 7, samples, 4, 153, digits, sizeof(digits), NULL, NULL));
  meta.check = CHK_CODE_PURIFIER;
  TEST_ASSERT_FALSE(chk_code_pack(&meta, fields, 7, samples, 4, 153, digits, sizeof(digits), NULL, NULL));
}

static void test_the_offset_is_carried_or_declared_unknown() {
  // the same result three ways: with the room's offset, with no zone set, and
  // with a zone half an hour off, which the field holds to the quarter hour.
  // Each must pack, and each must pack differently, since the page reads the
  // offset back and shows the time in it
  float samples[8] = {600, 601, 602, 603, 604, 605, 606, 607};
  const float fields[] = {2.9f, 0.15f, 0.99f, 843, 607, 425, 6};

  chk_code_meta_t meta = TEST_META(CHK_CODE_VENT);
  char known[CHK_CODE_MAX_DIGITS];
  TEST_ASSERT_TRUE(chk_code_pack(&meta, fields, 7, samples, 8, 153, known, sizeof(known), NULL, NULL));

  meta.offset = CHK_CODE_OFFSET_UNKNOWN;
  char unknown[CHK_CODE_MAX_DIGITS];
  TEST_ASSERT_TRUE(chk_code_pack(&meta, fields, 7, samples, 8, 153, unknown, sizeof(unknown), NULL, NULL));
  TEST_ASSERT_TRUE_MESSAGE(strcmp(known, unknown) != 0, "an unknown offset must not read as a zone");

  meta.offset = 330;  // Kolkata
  char half[CHK_CODE_MAX_DIGITS];
  TEST_ASSERT_TRUE(chk_code_pack(&meta, fields, 7, samples, 8, 153, half, sizeof(half), NULL, NULL));
  TEST_ASSERT_TRUE(strcmp(known, half) != 0 && strcmp(unknown, half) != 0);

  // the extremes fit the field rather than wrapping
  meta.offset = -720;
  TEST_ASSERT_TRUE(chk_code_pack(&meta, fields, 7, samples, 8, 153, half, sizeof(half), NULL, NULL));
  meta.offset = 840;
  TEST_ASSERT_TRUE(chk_code_pack(&meta, fields, 7, samples, 8, 153, half, sizeof(half), NULL, NULL));
}

static void test_a_field_past_its_width_saturates() {
  // ach is twelve bits of hundredths, so 40.95 is the most it holds. A breath
  // on the sensor once produced 42.4 and the whole share failed over it; the
  // encoder now stores the ceiling instead, and the payload is the same as
  // for a rate exactly at the ceiling rather than a wrapped or refused one
  float samples[8] = {600, 601, 602, 603, 604, 605, 606, 607};
  const chk_code_meta_t meta = {.check = CHK_CODE_VENT};

  const float ceiling[] = {40.95f, 0.15f, 0.99f, 843, 600, 425, 6};
  char want[CHK_CODE_MAX_DIGITS];
  TEST_ASSERT_TRUE(chk_code_pack(&meta, ceiling, 7, samples, 8, 153, want, sizeof(want), NULL, NULL));

  const float past[] = {42.4f, 0.15f, 0.99f, 843, 600, 425, 6};
  char got[CHK_CODE_MAX_DIGITS];
  TEST_ASSERT_TRUE_MESSAGE(chk_code_pack(&meta, past, 7, samples, 8, 153, got, sizeof(got), NULL, NULL),
                           "a value past its field must not lose the result");
  TEST_ASSERT_EQUAL_STRING(want, got);

  // and c0, thirteen bits of ppm, the same way
  const float c0_ceiling[] = {2.9f, 0.15f, 0.99f, 8191, 600, 425, 6};
  const float c0_past[] = {2.9f, 0.15f, 0.99f, 90000, 600, 425, 6};
  TEST_ASSERT_TRUE(chk_code_pack(&meta, c0_ceiling, 7, samples, 8, 153, want, sizeof(want), NULL, NULL));
  TEST_ASSERT_TRUE(chk_code_pack(&meta, c0_past, 7, samples, 8, 153, got, sizeof(got), NULL, NULL));
  TEST_ASSERT_EQUAL_STRING(want, got);
}

// the page's own CRC-8, written out again here so the test does not share the
// encoder's implementation: SMBus, polynomial 0x07, zero init and xorout
static uint8_t reference_crc8(const uint8_t* bytes, size_t len) {
  uint8_t c = 0;
  for (size_t i = 0; i < len; i++) {
    c ^= bytes[i];
    for (int k = 0; k < 8; k++) {
      c = (uint8_t)((c & 0x80) ? ((c << 1) ^ 0x07) : (c << 1));
    }
  }
  return c;
}

// reads the digits back into bytes, the way the page does: repeated division
// by 256 of the decimal number, which is slow but independent of the encoder
static size_t digits_to_bytes(const char* digits, uint8_t* out, size_t out_len) {
  uint8_t dec[CHK_CODE_MAX_DIGITS];
  size_t n = strlen(digits);
  for (size_t i = 0; i < n; i++) {
    dec[i] = (uint8_t)(digits[i] - '0');
  }
  uint8_t rev[CHK_CODE_MAX_BYTES];
  size_t len = 0;
  size_t start = 0;
  while (start < n) {
    int rem = 0;
    for (size_t i = start; i < n; i++) {
      int cur = rem * 10 + dec[i];
      dec[i] = (uint8_t)(cur / 256);
      rem = cur % 256;
    }
    while (start < n && dec[start] == 0) {
      start++;
    }
    if (len >= out_len) {
      return 0;
    }
    rev[len++] = (uint8_t)rem;
  }
  for (size_t i = 0; i < len; i++) {
    out[i] = rev[len - 1 - i];
  }
  return len;
}

static void test_the_payload_ends_with_its_checksum() {
  TEST_ASSERT_EQUAL_HEX8(0xf4, reference_crc8((const uint8_t*)"123456789", 9));

  uint8_t bytes[CHK_CODE_MAX_BYTES];
  size_t len = digits_to_bytes(VENT_DIGITS, bytes, sizeof(bytes));
  TEST_ASSERT_EQUAL_size_t(46, len);
  TEST_ASSERT_EQUAL_HEX8(reference_crc8(bytes, len - 1), bytes[len - 1]);

  len = digits_to_bytes(STOVE_DIGITS, bytes, sizeof(bytes));
  TEST_ASSERT_EQUAL_size_t(119, len);
  TEST_ASSERT_EQUAL_HEX8(reference_crc8(bytes, len - 1), bytes[len - 1]);
}

static void test_the_digits_are_only_digits() {
  // the payload rides in a numeric-mode QR segment, which has no other alphabet
  float samples[72];
  vent_samples(samples, 72);
  const float fields[] = {2.9f, 0.15f, 0.99f, 843, samples[71], 425, 6};
  const chk_code_meta_t meta = TEST_META(CHK_CODE_VENT);

  char digits[CHK_CODE_MAX_DIGITS];
  TEST_ASSERT_TRUE(chk_code_pack(&meta, fields, 7, samples, 72, 153, digits, sizeof(digits), NULL, NULL));

  for (const char* p = digits; *p != '\0'; p++) {
    TEST_ASSERT_TRUE_MESSAGE(*p >= '0' && *p <= '9', "the payload must be all digits");
  }
  // and never a leading zero, which would be lost converting back to bytes
  TEST_ASSERT_NOT_EQUAL('0', digits[0]);
}

// Encodes the link the way the screen does, but with buffers of its own that
// cannot alias, and returns the symbol. The firmware once handed the library
// one array for both scratch and output, which the library forbids: the
// symbol came out with half its modules wrong and every size test still
// passed, so this is checked module by module.
static int reference_symbol(const char* digits, uint8_t* out) {
  static uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];
  static uint8_t head_buf[64];
  char head[64];
  snprintf(head, sizeof(head), "%s%c", CHK_CODE_PREFIX, CHK_CODE_LETTER);
  memcpy(head_buf, head, strlen(head));

  static uint8_t num_buf[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];
  struct qrcodegen_Segment segs[2];
  segs[0] = qrcodegen_makeBytes(head_buf, strlen(head), temp);
  segs[1] = qrcodegen_makeNumeric(digits, num_buf);

  static uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];
  if (!qrcodegen_encodeSegmentsAdvanced(segs, 2, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 40,
                                        qrcodegen_Mask_AUTO, true, scratch, out)) {
    return -1;
  }
  return (qrcodegen_getSize(out) - 17) / 4;
}

static void test_the_symbol_matches_an_independent_encoding() {
  // the longest of the two payloads, so the symbol is at the panel's limit
  static uint8_t got[CHK_CODE_QR_BUFFER_LEN];
  static uint8_t want[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];
  TEST_ASSERT_TRUE(chk_code_symbol(STOVE_DIGITS, got));

  int version = reference_symbol(STOVE_DIGITS, want);
  TEST_ASSERT_TRUE(version > 0 && version <= CHK_CODE_QR_MAX_VERSION);

  int size = qrcodegen_getSize(want);
  TEST_ASSERT_EQUAL_INT(size, qrcodegen_getSize(got));
  int wrong = 0;
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      if (qrcodegen_getModule(got, x, y) != qrcodegen_getModule(want, x, y)) {
        wrong++;
      }
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, wrong, "every module must match an encoding with separate buffers");
}

static void test_a_link_past_the_panel_is_refused() {
  // 369 digits is the most a version 9 symbol takes behind the prefix; a
  // longer payload must be refused rather than drawn as a symbol that does
  // not fit
  static char digits[512];
  for (int i = 0; i < 420; i++) {
    digits[i] = (char)('0' + (i * 7) % 10);
  }
  digits[420] = '\0';

  static uint8_t out[CHK_CODE_QR_BUFFER_LEN];
  TEST_ASSERT_FALSE(chk_code_symbol(digits, out));

  // and something that is not digits at all
  TEST_ASSERT_FALSE(chk_code_symbol("12x4", out));
  TEST_ASSERT_FALSE(chk_code_symbol("", out));
}

void suite_chk_code() {
  RUN_TEST(test_the_symbol_matches_an_independent_encoding);
  RUN_TEST(test_a_link_past_the_panel_is_refused);
  RUN_TEST(test_a_ventilation_payload_matches_the_page);
  RUN_TEST(test_a_stove_payload_matches_the_page);
  RUN_TEST(test_a_long_check_loses_resolution_rather_than_failing);
  RUN_TEST(test_an_unknown_check_is_refused);
  RUN_TEST(test_the_offset_is_carried_or_declared_unknown);
  RUN_TEST(test_a_field_past_its_width_saturates);
  RUN_TEST(test_the_digits_are_only_digits);
  RUN_TEST(test_the_payload_ends_with_its_checksum);
}
