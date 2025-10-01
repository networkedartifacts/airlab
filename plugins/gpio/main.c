#include <stdbool.h>

#include "../al.h"

int main() {
  // configure GPIOs
  al_gpio(AL_GPIO_CONFIG, AL_GPIO_A); // output
  al_gpio(AL_GPIO_CONFIG, AL_GPIO_B | AL_GPIO_INPUT | AL_GPIO_PULL_UP);

  // clear screen
  al_clear(0);

  // write text
  al_write(10, 10, 16, 1, "GPIO Test");

  bool gpio_a = false;

  // draw borders for GPIO indicators
  al_rect(103, 49, 30, 30, 1, 4);
  al_rect(163, 49, 30, 30, 1, 4);

  for (;;) {
    // read GPIO B
    bool gpio_b = al_gpio(AL_GPIO_READ, AL_GPIO_B) == 1;

    // show rectangles based on levels
    al_rect(103 + 2, 49 + 2, 20, 20, gpio_a ? 1 : 0, 0);
    al_rect(163 + 2, 49 + 2, 20, 20, gpio_b ? 1 : 0, 0);

    // await event
    al_yield_result_t res = al_yield(500, 0);

    // stop on escape
    if (res == AL_YIELD_ESCAPE) {
      break;
    }

    // toggle GPIO A on enter
    if (res == AL_YIELD_ENTER) {
      gpio_a = !gpio_a;
      al_gpio(AL_GPIO_WRITE, AL_GPIO_A | (gpio_a ? AL_GPIO_HIGH : 0));
    }
  }

  return 0;
}
