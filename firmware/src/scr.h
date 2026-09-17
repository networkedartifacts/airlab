#ifndef SCR_H
#define SCR_H

#include <al/core.h>

// the display refresh intervals the doze loop accepts (s), mirrored by the
// power model in src/pwr.c
#define SCR_DISPLAY_MIN 60
#define SCR_DISPLAY_MAX 300

void scr_run(al_trigger_t trigger);

// Parks the given screen across a sleep: sleeps for the given duration while
// holding the given sampling interval, leaving whatever is on the display in
// place and waking back into that screen rather than the idle one. Does not
// return when it sleeps. It returns only when the device must stay awake, in
// which case it has woken up fully, the same as the idle screen does. Waking
// up applies the main interval, so the held interval is dropped by a refused
// or aborted sleep.
void scr_park(int32_t interval_s, int32_t duration_ms, void* resume);

// Reports whether the awake configuration is applied, which it is for a
// present user or a connected client and is not on a wake up the device
// performs on its own, to refresh the display or move the stores along.
bool scr_awake(void);

#endif  // SCR_H
