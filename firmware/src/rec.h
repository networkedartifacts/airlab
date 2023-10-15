#ifndef REC_H
#define REC_H

#include <stdbool.h>

#include "dat.h"

void rec_init();
uint32_t rec_free(bool new);
size_t rec_file();
bool rec_running();
void rec_start(size_t file);
void rec_mark();
void rec_stop();

#endif  // REC_H
