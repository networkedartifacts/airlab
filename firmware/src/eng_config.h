#ifndef ENG_CONFIG_H
#define ENG_CONFIG_H

#include <stdbool.h>

#include "eng_bundle.h"

// eng_config_get_s returns a string config value. Returns length copied, or 0 on error.
int eng_config_get_s(eng_bundle_t *config_values, eng_bundle_t *config_schema, const char *key, char *value, int value_len);

// eng_config_get_b returns a bool config value. Returns false on error.
bool eng_config_get_b(eng_bundle_t *config_values, eng_bundle_t *config_schema, const char *key);

// eng_config_get_i returns an int config value. Returns 0 on error.
int eng_config_get_i(eng_bundle_t *config_values, eng_bundle_t *config_schema, const char *key);

// eng_config_get_f returns a float config value. Returns 0 on error.
float eng_config_get_f(eng_bundle_t *config_values, eng_bundle_t *config_schema, const char *key);

#endif  // ENG_CONFIG_H
