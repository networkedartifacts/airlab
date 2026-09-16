#include <string.h>

#include <unity.h>

#include "qrcodegen.h"

// The share link is a byte-mode segment for the prefix and a numeric-mode
// segment for the payload digits. That split is what makes a result fit the
// screen: numeric mode packs three digits into ten bits, where byte mode
// would spend twenty-four.
#define QR_PREFIX "https://airlab.today/ac/A"

// What the 296x128 screen allows at a 2 px module size with a four-module
// quiet zone: a version 9 symbol at error correction level M.
#define QR_VERSION 9
#define QR_ECC qrcodegen_Ecc_MEDIUM

// builds the link and returns the symbol version, or -1 when it will not fit
static int encode_link(const char *digits, int max_version) {
  uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];
  uint8_t out[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];

  // the prefix goes in byte mode, the payload in numeric
  uint8_t prefix_buf[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];
  memcpy(prefix_buf, QR_PREFIX, strlen(QR_PREFIX));
  struct qrcodegen_Segment segs[2];
  segs[0] = qrcodegen_makeBytes(prefix_buf, strlen(QR_PREFIX), temp);
  segs[1] = qrcodegen_makeNumeric(digits, temp + qrcodegen_BUFFER_LEN_FOR_VERSION(40) / 2);

  if (!qrcodegen_encodeSegments(segs, 2, QR_ECC, out, out)) {
    return -1;
  }
  int size = qrcodegen_getSize(out);
  (void)max_version;
  return (size - 17) / 4;  // size = 4 * version + 17
}

static void fill_digits(char *buf, int n) {
  for (int i = 0; i < n; i++) {
    buf[i] = (char)('0' + (i * 7) % 10);
  }
  buf[n] = '\0';
}

static void test_a_ventilation_payload_fits_the_screen() {
  // 66 bytes of payload packs into 159 digits
  char digits[512];
  fill_digits(digits, 159);

  int version = encode_link(digits, QR_VERSION);
  TEST_ASSERT_TRUE_MESSAGE(version > 0, "the ventilation link must encode");
  TEST_ASSERT_TRUE_MESSAGE(version <= QR_VERSION, "and must fit a version 9 symbol at level M");
}

static void test_a_gas_stove_payload_fits_the_screen() {
  // the longest of the three built checks: 132 bytes, 318 digits
  char digits[512];
  fill_digits(digits, 318);

  int version = encode_link(digits, QR_VERSION);
  TEST_ASSERT_TRUE_MESSAGE(version > 0, "the stove link must encode");
  TEST_ASSERT_TRUE_MESSAGE(version <= QR_VERSION, "and must fit a version 9 symbol at level M");
}

static void test_the_budget_has_a_ceiling() {
  // 153 bytes is the budget, so 369 digits is the most that fits and a
  // meaningfully longer payload must not quietly grow the symbol
  char digits[1024];
  fill_digits(digits, 369);
  TEST_ASSERT_TRUE(encode_link(digits, QR_VERSION) <= QR_VERSION);

  fill_digits(digits, 420);
  int version = encode_link(digits, QR_VERSION);
  TEST_ASSERT_TRUE_MESSAGE(version > QR_VERSION, "a payload past the budget must need a larger symbol");
}

// The screen is 128 px tall, so at the 2 px module size a symbol may be at
// most 64 modules across including its quiet zone. Version 9 is 53, version
// 14 is 73.
#define QR_MAX_MODULES 64

static void test_the_segment_split_is_what_makes_it_fit() {
  // encodeText does NOT split the link into segments: the prefix has
  // lowercase letters, so the whole string becomes one byte-mode segment and
  // every payload digit costs eight bits instead of the three and a third
  // numeric mode spends.
  char digits[512];
  fill_digits(digits, 318);

  uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];
  uint8_t out[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];
  char whole[600];
  snprintf(whole, sizeof(whole), "%s%s", QR_PREFIX, digits);

  TEST_ASSERT_TRUE(qrcodegen_encodeText(whole, temp, out, QR_ECC, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                        qrcodegen_Mask_AUTO, true));
  int as_text = (qrcodegen_getSize(out) - 17) / 4;
  int as_split = encode_link(digits, QR_VERSION);

  // the split is the whole reason this fits the screen
  TEST_ASSERT_TRUE_MESSAGE(as_split < as_text, "splitting the link must beat encoding it whole");
  TEST_ASSERT_TRUE_MESSAGE(4 * as_split + 17 <= QR_MAX_MODULES, "the split symbol must fit the screen at 2 px");
  TEST_ASSERT_TRUE_MESSAGE(4 * as_text + 17 > QR_MAX_MODULES, "and the whole-string symbol must not, or this test is moot");
}

void suite_qrcodegen() {
  RUN_TEST(test_a_ventilation_payload_fits_the_screen);
  RUN_TEST(test_a_gas_stove_payload_fits_the_screen);
  RUN_TEST(test_the_budget_has_a_ceiling);
  RUN_TEST(test_the_segment_split_is_what_makes_it_fit);
}
