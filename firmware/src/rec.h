#ifndef REC_H
#define REC_H

#include <stdbool.h>

#include "dat.h"

void rec_init();
dat_file_t* rec_file();
bool rec_running();
void rec_start(dat_file_t* file);
void rec_stop();

#endif  // REC_H
