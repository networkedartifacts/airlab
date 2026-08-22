#ifndef NAOS_SYS_H
#define NAOS_SYS_H

#include <stddef.h>

// host shim for lib code under test

typedef void *naos_mutex_t;

static inline naos_mutex_t naos_mutex() { return NULL; }
static inline void naos_lock(naos_mutex_t mutex) { (void)mutex; }
static inline void naos_unlock(naos_mutex_t mutex) { (void)mutex; }

#endif  // NAOS_SYS_H
