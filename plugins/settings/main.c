#include "../al.h"

int main() {
  // clear screen
  al_clear(0);

  // get settings
  char s[32];
  int sl = al_settings_get_s("string", s, sizeof(s));
  bool b = al_settings_get_b("bool");
  int i = al_settings_get_i("int");
  float f = al_settings_get_f("float");
  char se[32];
  int sel = al_settings_get_s("string-enum", se, sizeof(se));
  int ie = al_settings_get_i("int-enum");

  // format string
  char buf[32];
  snprintf(buf, sizeof(buf), "%s / %d / %d / %f\n%s / %d", s, b, i, f, se, ie);

  // write text
  al_write(AL_W / 2, (AL_H - 16) / 2, 0, 16, 1, buf, AL_WRITE_ALIGN_CENTER);

  // wait for an event
  al_yield(0, 0);

  return 0;
}
