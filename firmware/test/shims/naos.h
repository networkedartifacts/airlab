#ifndef NAOS_H
#define NAOS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// host shim for lib code under test

static inline void naos_log(const char *fmt, ...) { (void)fmt; }

#define NAOS_COUNT(x) (sizeof(x) / sizeof(x[0]))

typedef enum {
  NAOS_LONG,
} naos_type_t;

typedef struct {
  const char *name;
  naos_type_t type;
  int32_t *sync_l;
  int32_t default_l;
} naos_param_t;

// minimal parameter registry backing naos_register/naos_set_l

typedef struct {
  naos_param_t *items[16];
  size_t count;
} naos_shim_registry_t;

static inline naos_shim_registry_t *naos_shim_registry(void) {
  static naos_shim_registry_t registry = {0};
  return &registry;
}

static inline void naos_register(naos_param_t *param) {
  // apply default
  if (param->sync_l != NULL) {
    *param->sync_l = param->default_l;
  }

  // add or replace param
  naos_shim_registry_t *registry = naos_shim_registry();
  for (size_t i = 0; i < registry->count; i++) {
    if (strcmp(registry->items[i]->name, param->name) == 0) {
      registry->items[i] = param;
      return;
    }
  }
  if (registry->count < NAOS_COUNT(registry->items)) {
    registry->items[registry->count++] = param;
  }
}

static inline void naos_set_l(const char *name, int32_t value) {
  naos_shim_registry_t *registry = naos_shim_registry();
  for (size_t i = 0; i < registry->count; i++) {
    if (strcmp(registry->items[i]->name, name) == 0 && registry->items[i]->sync_l != NULL) {
      *registry->items[i]->sync_l = value;
    }
  }
}

#endif  // NAOS_H
