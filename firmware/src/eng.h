#ifndef ENG_H
#define ENG_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
  char file[64];
  char name[64];
  char title[64];
  char version[64];
  size_t size;
} eng_plugin_t;

void eng_reload();
size_t eng_num();
eng_plugin_t* eng_get(size_t index);

bool eng_run(const char* file, const char* binary);

#endif  // ENG_H
