#ifndef NAOS_H
#define NAOS_H

// host shim for lib code under test

static inline void naos_log(const char *fmt, ...) { (void)fmt; }

#endif  // NAOS_H
