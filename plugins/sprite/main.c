#include "../al.h"

int main() {
  // clear screen
  al_clear(0);

  // resolve sprite
  int sprite = al_sprite_resolve("sprite");

  // draw sprite
  al_sprite_draw(sprite, 0, 0, 1);

  // wait for an event
  al_yield(0, 0);

  return 0;
}
