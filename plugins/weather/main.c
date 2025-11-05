#include "../al.h"

int main() {
  // clear screen
  al_clear(0);

  // write text
  al_write(AL_W / 2, (AL_H - 16) / 2, 0, 16, 1, "Making Request...", AL_WRITE_ALIGN_CENTER);

  // prepare buffer
  char *res = calloc(1024, 1);

  // make request
  al_http_new();
  al_http_set(ENG_HTTP_URL, 0, "https://f.networkedartifacts.com/call/weather?cty=Zurich", NULL);
  al_http_set(ENG_HTTP_METHOD, 0, "GET", NULL);
  al_http_set(ENG_HTTP_TIMEOUT, 2000, NULL, NULL);
  al_http_run(NULL, 0, res, 1024);

  // prepare temperatures
  float max_temps[3] = {0};
  float min_temps[3] = {0};

  // parse result
  int i = 0;
  char *line = strtok(res, "\n");
  while (line) {
    float x, y, z;
    if (sscanf(line, "%f,%f,%f", &x, &y, &z) == 3) {
      if (i == 0) {
        max_temps[0] = x;
        max_temps[1] = y;
        max_temps[2] = z;
      } else if (i == 1) {
        min_temps[0] = x;
        min_temps[1] = y;
        min_temps[2] = z;
      }
    }
    line = strtok(NULL, "\n");
    i++;
  }

  // format result
  char line1[64], line2[64];
  sprintf(line1, "Max temps: %.1f, %.1f, %.1f\n", max_temps[0], max_temps[1], max_temps[2]);
  sprintf(line2, "Min temps: %.1f, %.1f, %.1f\n", min_temps[0], min_temps[1], min_temps[2]);

  // write result
  al_clear(0);
  al_write(AL_W / 2, AL_H / 2 - 18, 0, 16, 1, line1, AL_WRITE_ALIGN_CENTER);
  al_write(AL_W / 2, AL_H / 2 + 2, 0, 16, 1, line2, AL_WRITE_ALIGN_CENTER);

  // wait for an event
  al_yield(0, 0);

  return 0;
}
