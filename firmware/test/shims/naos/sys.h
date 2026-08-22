#ifndef NAOS_SYS_H
#define NAOS_SYS_H

#include <stddef.h>
#include <stdint.h>

// host shim for lib code under test

typedef void *naos_mutex_t;

static inline naos_mutex_t naos_mutex() { return NULL; }
static inline void naos_lock(naos_mutex_t mutex) { (void)mutex; }
static inline void naos_unlock(naos_mutex_t mutex) { (void)mutex; }

typedef void (*naos_func_t)(void);
typedef void *naos_timer_t;

// defined by suites that exercise time-based code
int64_t naos_millis();
naos_timer_t naos_repeat_defer(const char *name, uint32_t period_ms, naos_func_t func);

#endif  // NAOS_SYS_H
