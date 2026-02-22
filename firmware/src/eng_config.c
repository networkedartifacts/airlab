#include <string.h>

#include "eng_config.h"

// config types
enum {
  ENG_CONFIG_SECTION = 0,
  ENG_CONFIG_STRING = 1,
  ENG_CONFIG_BOOL = 2,
  ENG_CONFIG_INT = 3,
  ENG_CONFIG_FLOAT = 4,
};

// bundle item flags
enum {
  ENG_CONFIG_FLAG_DEFAULT = (1 << 0),
  ENG_CONFIG_FLAG_HINT = (1 << 5),
};

static uint32_t eng_config_le32(const void *buf) {
  uint32_t val;
  memcpy(&val, buf, sizeof(val));
  return val;
}

// eng_config_default_find locates the default value in a bundle config
// item. It returns the offset to the default value, or -1 on error.
static int eng_config_default_find(const uint8_t *data, size_t len, uint8_t *type) {
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
  if (flags & ENG_CONFIG_FLAG_HINT) {
    while (off < (int)len && data[off] != 0) {
      off++;
    }
    if (off >= (int)len) {
      return -1;
    }
    off++;  // skip null
  }

  // check if default is present
  if (!(flags & ENG_CONFIG_FLAG_DEFAULT)) {
    return -1;
  }

  return off;
}

int eng_config_get_s(eng_bundle_t *config_values, eng_bundle_t *config_schema, const char *key, char *value, int value_len) {
  // check stored config first
  if (config_values) {
    size_t slen = 0;
    uint8_t *sdata = eng_bundle_binary(config_values, key, &slen);
    if (sdata && slen >= 1 && sdata[0] == ENG_CONFIG_STRING) {
      const char *str = (const char *)(sdata + 1);
      int str_len = strnlen(str, slen - 1);
      int copy_len = str_len < value_len ? str_len : value_len;
      memcpy(value, str, copy_len);
      return copy_len;
    }
  }

  // fall back to bundle default
  size_t len = 0;
  uint8_t *data = eng_bundle_binary(config_schema, key, &len);
  if (!data) {
    return 0;
  }

  uint8_t type;
  int off = eng_config_default_find(data, len, &type);
  if (off < 0 || type != ENG_CONFIG_STRING) {
    return 0;
  }

  const char *str = (const char *)(data + off);
  int str_len = strnlen(str, len - off);
  int copy_len = str_len < value_len ? str_len : value_len;
  memcpy(value, str, copy_len);

  return copy_len;
}

bool eng_config_get_b(eng_bundle_t *config_values, eng_bundle_t *config_schema, const char *key) {
  // check stored config first
  if (config_values) {
    size_t slen = 0;
    uint8_t *sdata = eng_bundle_binary(config_values, key, &slen);
    if (sdata && slen >= 2 && sdata[0] == ENG_CONFIG_BOOL) {
      return sdata[1] != 0;
    }
  }

  // fall back to bundle default
  size_t len = 0;
  uint8_t *data = eng_bundle_binary(config_schema, key, &len);
  if (!data) {
    return false;
  }

  uint8_t type;
  int off = eng_config_default_find(data, len, &type);
  if (off < 0 || type != ENG_CONFIG_BOOL) {
    return false;
  }

  if (off + 1 > (int)len) {
    return false;
  }

  return data[off] != 0;
}

int eng_config_get_i(eng_bundle_t *config_values, eng_bundle_t *config_schema, const char *key) {
  // check stored config first
  if (config_values) {
    size_t slen = 0;
    uint8_t *sdata = eng_bundle_binary(config_values, key, &slen);
    if (sdata && slen >= 5 && sdata[0] == ENG_CONFIG_INT) {
      return (int32_t)eng_config_le32(sdata + 1);
    }
  }

  // fall back to bundle default
  size_t len = 0;
  uint8_t *data = eng_bundle_binary(config_schema, key, &len);
  if (!data) {
    return 0;
  }

  uint8_t type;
  int off = eng_config_default_find(data, len, &type);
  if (off < 0 || type != ENG_CONFIG_INT) {
    return 0;
  }

  if (off + 4 > (int)len) {
    return 0;
  }

  return (int32_t)eng_config_le32(data + off);
}

float eng_config_get_f(eng_bundle_t *config_values, eng_bundle_t *config_schema, const char *key) {
  // check stored config first
  if (config_values) {
    size_t slen = 0;
    uint8_t *sdata = eng_bundle_binary(config_values, key, &slen);
    if (sdata && slen >= 5 && sdata[0] == ENG_CONFIG_FLOAT) {
      float val;
      uint32_t bits = eng_config_le32(sdata + 1);
      memcpy(&val, &bits, sizeof(val));
      return val;
    }
  }

  // fall back to bundle default
  size_t len = 0;
  uint8_t *data = eng_bundle_binary(config_schema, key, &len);
  if (!data) {
    return 0;
  }

  uint8_t type;
  int off = eng_config_default_find(data, len, &type);
  if (off < 0 || type != ENG_CONFIG_FLOAT) {
    return 0;
  }

  if (off + 4 > (int)len) {
    return 0;
  }

  float val;
  uint32_t bits = eng_config_le32(data + off);
  memcpy(&val, &bits, sizeof(val));

  return val;
}
