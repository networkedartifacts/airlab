#ifndef REC_H
#define REC_H

#include <stdbool.h>

#include "dat.h"

void rec_init();
uint32_t rec_free(bool new);
uint16_t rec_file();
bool rec_running();
void rec_start(uint16_t file);
void rec_mark();
void rec_stop();

#endif  // REC_H
