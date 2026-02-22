#ifndef ENG_SETTINGS_H
#define ENG_SETTINGS_H

#include <stdbool.h>

#include "eng_bundle.h"

// The stored settings use the standard bundle format. Each setting is stored
// as a BundleTypeBinary section named by key with the following data:
//
//   [type: 1 byte]  — 0=string, 1=bool, 2=int, 3=float
//   [value]
//
// Value encoding by type:
//   string — uint16 LE length + raw bytes
//   bool   — 1 byte (0 or 1)
//   int    — int32 little-endian
//   float  — float32 little-endian

// eng_settings_get_s returns a string setting. Returns length copied, or 0 on error.
int eng_settings_get_s(eng_bundle_t *settings_values, eng_bundle_t *settings_schema, const char *key, char *value, int value_len);

// eng_settings_get_b returns a bool setting. Returns false on error.
bool eng_settings_get_b(eng_bundle_t *settings_values, eng_bundle_t *settings_schema, const char *key);

// eng_settings_get_i returns an int setting. Returns 0 on error.
int eng_settings_get_i(eng_bundle_t *settings_values, eng_bundle_t *settings_schema, const char *key);

// eng_settings_get_f returns a float setting. Returns 0 on error.
float eng_settings_get_f(eng_bundle_t *settings_values, eng_bundle_t *settings_schema, const char *key);

#endif  // ENG_SETTINGS_H
