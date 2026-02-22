#ifndef ENG_EXEC_H
#define ENG_EXEC_H

#include "eng_bundle.h"

typedef enum {
  ENG_PERM_NETWORK = (1 << 0),      // HTTP operations
  ENG_PERM_INTERACTION = (1 << 1),  // yield (button input) and related config
  ENG_PERM_IO = (1 << 2),           // GPIO and I2C operations
  ENG_PERM_STORAGE = (1 << 3),      // data get/set operations
  ENG_PERM_AUDIO = (1 << 4),        // buzzer/beep
  ENG_PERM_GRAPHICS = (1 << 5),     // graphics operations (clear, draw, write, etc.)
  ENG_PERM_BLOCK = (1 << 6),        // blocking operations (yield, delay)
  ENG_PERM_ALL = 0xFFFFFFFF,        // all permissions
} eng_perm_t;

void *eng_exec_start(eng_bundle_t *bundle, const char *binary, eng_perm_t perms, eng_bundle_t *config_schema,
                     eng_bundle_t *config_values);
void eng_exec_wait(void *ref);

#endif  // ENG_EXEC_H
