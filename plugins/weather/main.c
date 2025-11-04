#include "../al.h"

#define SJ_IMPL
#include "../sj.h"

static bool eq(sj_Value val, char *s) {
  size_t len = val.end - val.start;
  return strlen(s) == len && !memcmp(s, val.start, len);
}

int main() {
  // clear screen
  al_clear(0);

  // write text
  al_write(AL_W / 2, (AL_H - 16) / 2, 0, 16, 1, "Making Request...", AL_WRITE_ALIGN_CENTER);

  // prepare buffer
  char *res = calloc(1024, 1);

  // make request
  al_http_new();
  al_http_set(ENG_HTTP_URL, 0,
              "http://api.open-meteo.com/v1/"
              "forecast?latitude=47.3667&longitude=8.55&daily=temperature_2m_max,temperature_2m_min&forecast_days=3",
              NULL);
  al_http_set(ENG_HTTP_METHOD, 0, "GET", NULL);
  al_http_set(ENG_HTTP_TIMEOUT, 1000, NULL, NULL);
  al_http_run(NULL, 0, res, 1024);

  // prepare temperatures
  float max_temps[3] = {0};
  float min_temps[3] = {0};

  // parse JSON
  sj_Reader r = sj_reader(res, strlen(res));
  sj_Value obj = sj_read(&r);

  // iterate root object
  sj_Value key, val;
  while (sj_iter_object(&r, obj, &key, &val)) {
    if (eq(key, "daily")) {
      // iterate daily object
      sj_Value v2, k2;
      while (sj_iter_object(&r, val, &k2, &v2)) {
        // iterate max temperatures
        if (eq(k2, "temperature_2m_max")) {
          int i = 0;
          sj_Value arr;
          while (sj_iter_array(&r, v2, &arr)) {
            char buf[16];
            snprintf(buf, sizeof(buf), arr.start, arr.end - arr.start);
            buf[arr.end - arr.start] = 0;
            max_temps[i] = strtof(buf, NULL);
            i++;
          }
        }

        // iterate min temperatures
        if (eq(k2, "temperature_2m_min")) {
          int i = 0;
          sj_Value arr;
          while (sj_iter_array(&r, v2, &arr)) {
            char buf[16];
            snprintf(buf, sizeof(buf), arr.start, arr.end - arr.start);
            buf[arr.end - arr.start] = 0;
            min_temps[i] = strtof(buf, NULL);
            i++;
          }
        }
      }
    }
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
