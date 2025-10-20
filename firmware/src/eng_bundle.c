#include <naos.h>
#include <string.h>
#include <esp_crc.h>

#include <al/core.h>
#include <al/storage.h>

#include "eng_bundle.h"

#define ENG_BUNDLE_DEBUG false

typedef struct {
  const uint8_t *buf;
  size_t len;
  size_t pos;
  size_t header_len;
  uint16_t sections;
  uint16_t current;
} eng_bundle_iter_t;

static uint16_t eng_bundle_le16(const void *buf) {
  uint16_t val;
  memcpy(&val, buf, sizeof(val));
  return val;
}

static uint32_t eng_bundle_le32(const void *buf) {
  uint32_t val;
  memcpy(&val, buf, sizeof(val));
  return val;
}

static size_t eng_bundle_strnlen(const char *s, size_t max_len) {
  const char *end = memchr(s, 0, max_len);
  return end ? (size_t)(end - s) : max_len;
}

static bool eng_bundle_iter_init(eng_bundle_iter_t *i, const void *buf, size_t len) {
  // check bundle header
  if (len < 10 || memcmp(buf, "ALP\0", 4) != 0) {
    naos_log("eng_bundle_iter_init: invalid bundle header");
    return false;
  }

  // get header length and number of sections
  uint32_t header_len = eng_bundle_le32(buf + 4);
  uint16_t sections = eng_bundle_le16(buf + 8);

  // initialize iterator
  *i = (eng_bundle_iter_t){
      .buf = buf,
      .len = len,
      .pos = 10,
      .header_len = header_len,
      .sections = sections,
  };

  return true;
}

static bool eng_bundle_iter_next(eng_bundle_iter_t *i, eng_bundle_section_t *s) {
  // check end of iteration
  if (i->current >= i->sections) {
    return false;
  }

  // check section header
  if (i->pos + 13 > i->len) {
    naos_log("eng_bundle_iter_next: incomplete section header");
    return false;
  }

  // get type and length
  s->type = i->buf[i->pos++];
  s->off = eng_bundle_le32(i->buf + i->pos);
  i->pos += 4;
  s->len = eng_bundle_le32(i->buf + i->pos);
  i->pos += 4;
  s->crc32 = eng_bundle_le32(i->buf + i->pos);
  i->pos += 4;

  // get name
  size_t name_len = strnlen((const char *)i->buf + i->pos, i->len - i->pos);
  if (i->pos + name_len + 1 > i->len) {
    naos_log("eng_bundle_iter_next: truncated section name");
    return false;
  }
  s->name = (const char *)i->buf + i->pos;
  i->pos += name_len + 1;

  // update iterator
  i->current++;

  return true;
}

eng_bundle_t *eng_bundle_load() {
  // get file size
  int size = al_storage_stat(AL_STORAGE_INT, "engine", "app.wasm");
  if (size < 0) {
    naos_log("eng_bundle_load: failed to stat bundle file");
    return NULL;
  }

  // reader bundle prefix
  uint8_t prefix[10];
  if (!al_storage_read(AL_STORAGE_INT, "engine", "app.wasm", prefix, 0, sizeof(prefix))) {
    naos_log("eng_bundle_load: failed to read bundle prefix");
    return NULL;
  }

  // parse bundle prefix
  eng_bundle_iter_t iter;
  if (!eng_bundle_iter_init(&iter, prefix, sizeof(prefix))) {
    return NULL;
  }

  // check header length
  if (iter.header_len > (size_t)size) {
    naos_log("eng_bundle_load: invalid bundle header length");
    return NULL;
  }

  // read bundle header
  uint8_t *header = al_alloc(iter.header_len);
  if (!al_storage_read(AL_STORAGE_INT, "engine", "app.wasm", header, 0, iter.header_len)) {
    naos_log("eng_bundle_load: failed to read bundle header");
    free(header);
    return NULL;
  }

  // prepare iterator
  if (!eng_bundle_iter_init(&iter, header, iter.header_len)) {
    free(header);
    return NULL;
  }

  // allocate bundle
  eng_bundle_t *b = al_alloc(sizeof(eng_bundle_t));

  // prepare bundle
  *b = (eng_bundle_t){
      .header = header,
      .header_len = iter.header_len,
  };

  // allocate sections
  b->sections = al_calloc(iter.sections, sizeof(eng_bundle_section_t));
  b->sections_num = iter.sections;

  // parse sections
  for (int i = 0; i < iter.sections; i++) {
    eng_bundle_section_t *s = &b->sections[i];
    if (!eng_bundle_iter_next(&iter, s)) {
      eng_bundle_free(b);
      return NULL;
    }
  }

  // print sections
  if (ENG_BUNDLE_DEBUG) {
    naos_log("eng_bundle_load: found %d sections", b->sections_num);
    for (int i = 0; i < b->sections_num; i++) {
      eng_bundle_section_t *section = &b->sections[i];
      naos_log("[%d]: type=%d name='%s' len=%zu", i, section->type, section->name, section->len);
    }
  }

  return b;
}

int eng_bundle_locate(eng_bundle_t *b, eng_bundle_type_t t, const char *name, eng_bundle_section_t **s) {
  // find matching section
  for (int i = 0; i < b->sections_num; i++) {
    if (b->sections[i].type == t && strcmp(b->sections[i].name, name) == 0) {
      if (s) {
        *s = &b->sections[i];
      }
      return i;
    }
  }

  return -1;
}

void *eng_bundle_read(eng_bundle_t *b, eng_bundle_section_t *s) {
  // return data if already loaded
  if (s->data) {
    return s->data;
  }

  // read data
  void *data = al_alloc(s->len);
  if (!al_storage_read(AL_STORAGE_INT, "engine", "app.wasm", data, s->off, s->len)) {
    naos_log("eng_bundle_read: failed to read section '%s'", s->name);
    free(data);
    return NULL;
  }

  // validate checksum
  uint32_t crc32 = esp_crc32_le(0, data, s->len);
  if (crc32 != s->crc32) {
    naos_log("eng_bundle_read: crc32 mismatch for section '%s'", s->name);
    free(data);
    return NULL;
  }

  // set data pointer
  s->data = data;

  return data;
}

void *eng_bundle_get(eng_bundle_t *b, eng_bundle_type_t t, const char *name, size_t *len) {
  // locate section
  eng_bundle_section_t *s;
  int idx = eng_bundle_locate(b, t, name, &s);
  if (idx < 0) {
    return NULL;
  }

  // read section
  void *data = eng_bundle_read(b, s);

  // set length
  if (len) {
    *len = s->len;
  }

  return data;
}

void eng_bundle_free(eng_bundle_t *b) {
  // free section data
  for (int i = 0; i < b->sections_num; i++) {
    eng_bundle_section_t *s = &b->sections[i];
    if (s->data) {
      free(s->data);
    }
  }

  // free sections
  if (b->sections) {
    free(b->sections);
  }

  // free buffer
  if (b->header) {
    free(b->header);
  }

  // free bundle
  free(b);
}

void *eng_bundle_attr(eng_bundle_t *b, const char *name, size_t *len) {
  // get attribute section
  return eng_bundle_get(b, ENG_BUNDLE_TYPE_ATTR, name, len);
}

void *eng_bundle_binary(eng_bundle_t *b, const char *name, size_t *len) {
  // get binary section
  return eng_bundle_get(b, ENG_BUNDLE_TYPE_BINARY, name, len);
}

bool eng_bundle_parse_sprite(eng_bundle_sprite_t *sp, eng_bundle_t *b, eng_bundle_section_t *sc) {
  // read section
  void *data = eng_bundle_read(b, sc);
  if (!data) {
    return false;
  }

  // get width and height
  uint16_t width, height;
  memcpy(&width, data, 2);
  memcpy(&height, data + 2, 2);

  // calculate size
  size_t size = ((size_t)width * (size_t)height + 7) / 8;

  // check size
  if (sc->len < 4 + size * 2) {
    return false;
  }

  // get image and mask
  const uint8_t *image = data + 4;
  const uint8_t *mask = image + size;

  // set sprite
  *sp = (eng_bundle_sprite_t){
      .width = width,
      .height = height,
      .image = image,
      .mask = mask,
  };

  return true;
}
