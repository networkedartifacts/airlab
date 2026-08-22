#ifndef SCR_H
#define SCR_H

#include <al/core.h>

// the display refresh intervals the doze loop accepts (s), mirrored by the
// power model in src/pwr.c
#define SCR_DISPLAY_MIN 60
#define SCR_DISPLAY_MAX 300

void scr_run(al_trigger_t trigger);

#endif  // SCR_H
