#include "../../al.h"

static void draw(const char *event, const char *detail) {
  al_clear(0);
  al_write(AL_W / 2, 20, 0, 16, 1, "Events", AL_WRITE_ALIGN_CENTER);
  al_write(AL_W / 2, 50, 0, 16, 1, event, AL_WRITE_ALIGN_CENTER);
  al_write(AL_W / 2, 70, 0, 16, 1, detail, AL_WRITE_ALIGN_CENTER);
}

int main() {
  // subscribe to all events
  al_yield_flags_t flags =
      AL_YIELD_SUB_TOUCH | AL_YIELD_SUB_SCROLL | AL_YIELD_SUB_MOTION | AL_YIELD_SUB_SENSOR | AL_YIELD_SUB_POWER;

  // draw initial screen
  draw("Waiting...", "");

  for (;;) {
    // await event
    al_yield_result_t res = al_yield(0, flags);

    // format detail
    char detail[64] = {0};

    switch (res) {
      case AL_YIELD_ESCAPE:
        return 0;
      case AL_YIELD_ENTER:
        draw("Enter", "");
        break;
      case AL_YIELD_UP:
        draw("Up", "");
        break;
      case AL_YIELD_DOWN:
        draw("Down", "");
        break;
      case AL_YIELD_LEFT:
        draw("Left", "");
        break;
      case AL_YIELD_RIGHT:
        draw("Right", "");
        break;
      case AL_YIELD_TOUCH: {
        float pos = al_info(AL_INFO_TOUCH_POS);
        snprintf(detail, sizeof(detail), "Pos: %.0f", pos);
        draw("Touch", detail);
        break;
      }
      case AL_YIELD_SCROLL: {
        float std = al_info(AL_INFO_SCROLL_STD);
        float fast = al_info(AL_INFO_SCROLL_FAST);
        snprintf(detail, sizeof(detail), "Std: %.0f Fast: %.0f", std, fast);
        draw("Scroll", detail);
        break;
      }
      case AL_YIELD_MOTION: {
        float front = al_info(AL_INFO_ACCEL_FRONT);
        float rotation = al_info(AL_INFO_ACCEL_ROTATION);
        snprintf(detail, sizeof(detail), "Front: %.0f Rot: %.0f", front, rotation);
        draw("Motion", detail);
        break;
      }
      case AL_YIELD_SENSOR: {
        float temp = al_info(AL_INFO_SENSOR_TEMPERATURE);
        float co2 = al_info(AL_INFO_SENSOR_CO2);
        snprintf(detail, sizeof(detail), "Temp: %.1f CO2: %.0f", temp, co2);
        draw("Sensor", detail);
        break;
      }
      case AL_YIELD_POWER: {
        float level = al_info(AL_INFO_BATTERY_LEVEL);
        float usb = al_info(AL_INFO_POWER_USB);
        snprintf(detail, sizeof(detail), "Bat: %.0f%% USB: %.0f", level * 100, usb);
        draw("Power", detail);
        break;
      }
      default:
        draw("Timeout", "");
        break;
    }
  }
}
