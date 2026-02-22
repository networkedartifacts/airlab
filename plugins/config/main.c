#include "../al.h"

int main() {
  // clear screen
  al_clear(0);

  // get config
  char s[32];
  int sl = al_config_get_s("string", s, sizeof(s));
  bool b = al_config_get_b("bool");
  int i = al_config_get_i("int");
  float f = al_config_get_f("float");
  char se[32];
  int sel = al_config_get_s("string-enum", se, sizeof(se));
  int ie = al_config_get_i("int-enum");

  // format string
  char buf[32];
  snprintf(buf, sizeof(buf), "%s / %d / %d / %f\n%s / %d", s, b, i, f, se, ie);

  // write text
  al_write(AL_W / 2, (AL_H - 16) / 2, 0, 16, 1, buf, AL_WRITE_ALIGN_CENTER);

  // wait for an event
  al_yield(0, 0);

  return 0;
}
