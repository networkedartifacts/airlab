#ifndef ENG_BUNDLE_H
#define ENG_BUNDLE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  ENG_BUNDLE_TYPE_ATTR = 0x00,
  ENG_BUNDLE_TYPE_BINARY = 0x01,
  ENG_BUNDLE_TYPE_SPRITE = 0x02,
} eng_bundle_type_t;

typedef struct {
  eng_bundle_type_t type;
  const char *name;
  size_t off;
  size_t len;
  uint32_t crc32;
  const uint8_t *data;
} eng_bundle_section_t;

typedef struct {
  const uint8_t *buf;
  size_t len;
  size_t pos;
  size_t header_len;
  uint16_t sections;
  uint16_t current;
} eng_bundle_iter_t;

typedef struct {
  void *buf;
  size_t buf_len;
  eng_bundle_section_t *sections;
  uint16_t sections_num;
} eng_bundle_t;

bool eng_bundle_iter_init(eng_bundle_iter_t *i, const void *buf, size_t len);
bool eng_bundle_iter_next(eng_bundle_iter_t *i, eng_bundle_section_t *s);

eng_bundle_t *eng_bundle_load();
int eng_bundle_locate(eng_bundle_t *b, eng_bundle_type_t t, const char *name, eng_bundle_section_t **s);
void eng_bundle_free(eng_bundle_t *b);

#endif  // ENG_BUNDLE_H
