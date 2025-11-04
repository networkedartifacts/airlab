#include "../al.h"

int main() {
  for (;;) {
    // clear screen
    al_clear(0);

    // get data
    int val = 0;
    al_data_get("value", &val, sizeof(val));

    // format string
    char buf[32];
    snprintf(buf, sizeof(buf), "Value: %d", val);

    // write text
    al_write(AL_W / 2, (AL_H - 16) / 2, 0, 16, 1, buf, AL_WRITE_ALIGN_CENTER);

    // wait for an event
    switch (al_yield(0, 0)) {
      case AL_YIELD_UP:
        val++;
        break;
      case AL_YIELD_DOWN:
        val--;
        break;
      case AL_YIELD_ESCAPE:
        return 0;
      default:
        continue;
    }

    // store data
    al_data_set("value", &val, sizeof(val));
  }

  return 0;
}
