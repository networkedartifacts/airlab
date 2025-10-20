#include <naos.h>
#include <string.h>
#include <esp_crc.h>

#include <al/core.h>
#include <al/storage.h>

#include "eng_bundle.h"

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

bool eng_bundle_iter_init(eng_bundle_iter_t *i, const void *buf, size_t len) {
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
      .header_len = header_len,
      .pos = 10,
      .sections = sections,
  };

  return true;
}

bool eng_bundle_iter_next(eng_bundle_iter_t *i, eng_bundle_section_t *s) {
  // check end of iteration
  if (i->current >= i->sections) {
    return false;
  }

  // check section header
  if (i->pos + 9 > i->len) {
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
  size_t name_len = strlen((const char *)i->buf + i->pos);
  if (i->pos + name_len + 1 + s->len > i->len) {
    naos_log("eng_bundle_iter_next: truncated section name");
    return false;
  }
  s->name = (const char *)i->buf + i->pos;
  i->pos += name_len + 1;

  // set data pointer
  s->data = i->buf + s->off;

  // validate checksum
  uint32_t crc32 = esp_crc32_le(0, s->data, s->len);
  if (crc32 != s->crc32) {
    naos_log("eng_bundle_iter_next: crc32 mismatch for section '%s'", s->name);
    return false;
  }

  // update iterator
  i->current++;

  return true;
}

eng_bundle_t *eng_bundle_load() {
  // read bundle
  size_t buf_len = 0;
  void *buf = al_storage_load(AL_STORAGE_INT, "engine", "app.wasm", &buf_len);
  if (!buf) {
    naos_log("eng: failed to read bundle");
    return NULL;
  }

  // allocate bundle
  eng_bundle_t *b = al_alloc(sizeof(eng_bundle_t));

  // prepare bundle
  *b = (eng_bundle_t){
      .buf = buf,
      .buf_len = buf_len,
  };

  // prepare iterator
  eng_bundle_iter_t iter;
  if (!eng_bundle_iter_init(&iter, buf, buf_len)) {
    return NULL;
  }

  // allocate sections
  b->sections = al_calloc(iter.sections, sizeof(eng_bundle_section_t));
  b->sections_num = iter.sections;

  // parse sections
  for (int i = 0; i < iter.sections; i++) {
    eng_bundle_section_t *s = &b->sections[i];
    if (!eng_bundle_iter_next(&iter, s)) {
      return NULL;
    }
  }

  return b;
}

int eng_bundle_locate(eng_bundle_t *b, eng_bundle_type_t t, const char *name, eng_bundle_section_t **s) {
  // find matching section
  for (int i = 0; i < b->sections_num; i++) {
    if (b->sections[i].type == t && strcmp(b->sections[i].name, name) == 0) {
      if (s != NULL) {
        *s = &b->sections[i];
      }
      return i;
    }
  }

  return -1;
}

void eng_bundle_free(eng_bundle_t *b) {
  // free buffer
  if (b->buf) {
    free(b->buf);
  }

  // free sections
  if (b->sections) {
    free(b->sections);
  }

  // free bundle
  free(b);
}
