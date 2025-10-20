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
  void *data;
} eng_bundle_section_t;

typedef struct {
  void *header;
  size_t header_len;
  eng_bundle_section_t *sections;
  uint16_t sections_num;
} eng_bundle_t;

eng_bundle_t *eng_bundle_load();
int eng_bundle_locate(eng_bundle_t *b, eng_bundle_type_t t, const char *name, eng_bundle_section_t **s);
void *eng_bundle_read(eng_bundle_t *b, eng_bundle_section_t *s);
void *eng_bundle_get(eng_bundle_t *b, eng_bundle_type_t t, const char *name, size_t *len);
void eng_bundle_free(eng_bundle_t *b);

void *eng_bundle_attr(eng_bundle_t *b, const char *name, size_t *len);
void *eng_bundle_binary(eng_bundle_t *b, const char *name, size_t *len);

typedef struct {
  uint16_t width;
  uint16_t height;
  const uint8_t *image;
  const uint8_t *mask;
} eng_bundle_sprite_t;

bool eng_bundle_parse_sprite(eng_bundle_sprite_t *sp, eng_bundle_t *b, eng_bundle_section_t *sc);

#endif  // ENG_BUNDLE_H
