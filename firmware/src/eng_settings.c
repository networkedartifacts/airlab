#include <string.h>

#include "eng_settings.h"

// settings types
enum {
  ENG_SETTINGS_STRING = 0,
  ENG_SETTINGS_BOOL = 1,
  ENG_SETTINGS_INT = 2,
  ENG_SETTINGS_FLOAT = 3,
};

// bundle item flags
enum {
  ENG_SETTINGS_FLAG_DEFAULT = (1 << 0),
  ENG_SETTINGS_FLAG_HINT = (1 << 5),
};

static uint16_t eng_settings_le16(const void *buf) {
  uint16_t val;
  memcpy(&val, buf, sizeof(val));
  return val;
}

static uint32_t eng_settings_le32(const void *buf) {
  uint32_t val;
  memcpy(&val, buf, sizeof(val));
  return val;
}

// eng_settings_default_find locates the default value in a bundle settings
// item. It returns the offset to the default value, or -1 on error.
static int eng_settings_default_find(const uint8_t *data, size_t len, uint8_t *type) {
  // check minimum length (section + type + flags + title null)
  if (len < 4) {
    return -1;
  }

  // get type and flags
  *type = data[1];
  uint8_t flags = data[2];
  int off = 3;

  // skip title (null-terminated)
  while (off < (int)len && data[off] != 0) {
    off++;
  }
  if (off >= (int)len) {
    return -1;
  }
  off++;  // skip null

  // skip hint (null-terminated) if flag set
  if (flags & ENG_SETTINGS_FLAG_HINT) {
    while (off < (int)len && data[off] != 0) {
      off++;
    }
    if (off >= (int)len) {
      return -1;
    }
    off++;  // skip null
  }

  // check if default is present
  if (!(flags & ENG_SETTINGS_FLAG_DEFAULT)) {
    return -1;
  }

  return off;
}

int eng_settings_get_s(eng_bundle_t *settings_values, eng_bundle_t *settings_schema, const char *key, char *value, int value_len) {
  // check stored settings first
  if (settings_values) {
    size_t slen = 0;
    uint8_t *sdata = eng_bundle_binary(settings_values, key, &slen);
    if (sdata && slen >= 1 && sdata[0] == ENG_SETTINGS_STRING) {
      int off = 1;
      if (off + 2 > (int)slen) {
        return 0;
      }
      uint16_t str_len = eng_settings_le16(sdata + off);
      off += 2;
      if (off + str_len > (int)slen) {
        return 0;
      }
      int copy_len = str_len < value_len ? str_len : value_len;
      memcpy(value, sdata + off, copy_len);
      return copy_len;
    }
  }

  // fall back to bundle default
  size_t len = 0;
  uint8_t *data = eng_bundle_binary(settings_schema, key, &len);
  if (!data) {
    return 0;
  }

  uint8_t type;
  int off = eng_settings_default_find(data, len, &type);
  if (off < 0 || type != ENG_SETTINGS_STRING) {
    return 0;
  }

  if (off + 2 > (int)len) {
    return 0;
  }
  uint16_t str_len = eng_settings_le16(data + off);
  off += 2;

  if (off + str_len > (int)len) {
    return 0;
  }

  int copy_len = str_len < value_len ? str_len : value_len;
  memcpy(value, data + off, copy_len);

  return copy_len;
}

bool eng_settings_get_b(eng_bundle_t *settings_values, eng_bundle_t *settings_schema, const char *key) {
  // check stored settings first
  if (settings_values) {
    size_t slen = 0;
    uint8_t *sdata = eng_bundle_binary(settings_values, key, &slen);
    if (sdata && slen >= 2 && sdata[0] == ENG_SETTINGS_BOOL) {
      return sdata[1] != 0;
    }
  }

  // fall back to bundle default
  size_t len = 0;
  uint8_t *data = eng_bundle_binary(settings_schema, key, &len);
  if (!data) {
    return false;
  }

  uint8_t type;
  int off = eng_settings_default_find(data, len, &type);
  if (off < 0 || type != ENG_SETTINGS_BOOL) {
    return false;
  }

  if (off + 1 > (int)len) {
    return false;
  }

  return data[off] != 0;
}

int eng_settings_get_i(eng_bundle_t *settings_values, eng_bundle_t *settings_schema, const char *key) {
  // check stored settings first
  if (settings_values) {
    size_t slen = 0;
    uint8_t *sdata = eng_bundle_binary(settings_values, key, &slen);
    if (sdata && slen >= 5 && sdata[0] == ENG_SETTINGS_INT) {
      return (int32_t)eng_settings_le32(sdata + 1);
    }
  }

  // fall back to bundle default
  size_t len = 0;
  uint8_t *data = eng_bundle_binary(settings_schema, key, &len);
  if (!data) {
    return 0;
  }

  uint8_t type;
  int off = eng_settings_default_find(data, len, &type);
  if (off < 0 || type != ENG_SETTINGS_INT) {
    return 0;
  }

  if (off + 4 > (int)len) {
    return 0;
  }

  return (int32_t)eng_settings_le32(data + off);
}

float eng_settings_get_f(eng_bundle_t *settings_values, eng_bundle_t *settings_schema, const char *key) {
  // check stored settings first
  if (settings_values) {
    size_t slen = 0;
    uint8_t *sdata = eng_bundle_binary(settings_values, key, &slen);
    if (sdata && slen >= 5 && sdata[0] == ENG_SETTINGS_FLOAT) {
      float val;
      uint32_t bits = eng_settings_le32(sdata + 1);
      memcpy(&val, &bits, sizeof(val));
      return val;
    }
  }

  // fall back to bundle default
  size_t len = 0;
  uint8_t *data = eng_bundle_binary(settings_schema, key, &len);
  if (!data) {
    return 0;
  }

  uint8_t type;
  int off = eng_settings_default_find(data, len, &type);
  if (off < 0 || type != ENG_SETTINGS_FLOAT) {
    return 0;
  }

  if (off + 4 > (int)len) {
    return 0;
  }

  float val;
  uint32_t bits = eng_settings_le32(data + off);
  memcpy(&val, &bits, sizeof(val));

  return val;
}
