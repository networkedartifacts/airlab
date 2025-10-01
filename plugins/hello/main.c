#include "../al.h"

int main() {
  // clear screen
  al_clear(0);

  // write text
  al_write(10, 10, 16, 1, "Hello, World!");

  // wait for an event
  al_yield(0, 0);

  return 0;
}
