#ifndef NAOS_SYS_H
#define NAOS_SYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// host shim for lib code under test

typedef void *naos_mutex_t;

static inline naos_mutex_t naos_mutex() { return NULL; }
static inline void naos_lock(naos_mutex_t mutex) { (void)mutex; }
static inline void naos_unlock(naos_mutex_t mutex) { (void)mutex; }

typedef void (*naos_func_t)(void);
typedef void *naos_timer_t;
typedef void *naos_task_t;

// defined by suites that exercise time-based code
int64_t naos_millis();
naos_timer_t naos_repeat_defer(const char *name, uint32_t period_ms, naos_func_t func);

// defined by suites that exercise task-starting code
naos_task_t naos_run(const char *name, uint16_t stack, int core, naos_func_t func);

// defined by suites that exercise deferring and delaying code
void naos_delay(uint32_t ms);
void naos_defer(const char *name, uint32_t delay_ms, naos_func_t func);
naos_timer_t naos_repeat(const char *name, uint32_t period_ms, naos_func_t func);

typedef void *naos_signal_t;

// defined by suites that exercise signalling code
naos_signal_t naos_signal();
void naos_trigger(naos_signal_t signal, uint16_t bits, bool clear);
bool naos_await(naos_signal_t signal, uint16_t bits, bool clear, int32_t timeout_ms);

#endif  // NAOS_SYS_H
