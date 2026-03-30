#include "../../al.h"

#include <math.h>

// main dial
#define CX 148
#define CY 64
#define R 58

// subdial radii and positions
#define SR 18
#define SUB_LX (CX - 32)
#define SUB_LY CY
#define SUB_RX (CX + 32)
#define SUB_RY CY
#define SUB_BX CX
#define SUB_BY (CY + 26)

// gauge dimensions
#define GR 22
#define GA_START 210
#define GA_END 330

static void hand(int cx, int cy, int len, int width, float angle) {
  float rad = (angle - 90.0f) * (float)M_PI / 180.0f;
  int x2 = cx + (int)(cosf(rad) * (float)len);
  int y2 = cy + (int)(sinf(rad) * (float)len);
  al_line(cx, cy, x2, y2, 1, width);
}

static void tick(int cx, int cy, int inner, int outer, float angle_deg, int width) {
  float rad = (angle_deg - 90.0f) * (float)M_PI / 180.0f;
  float c = cosf(rad);
  float s = sinf(rad);
  int x1 = cx + (int)(c * (float)inner);
  int y1 = cy + (int)(s * (float)inner);
  int x2 = cx + (int)(c * (float)outer);
  int y2 = cy + (int)(s * (float)outer);
  al_line(x1, y1, x2, y2, 1, width);
}

static void subdial_ring(int cx, int cy, int r, int count) {
  al_arc(cx, cy, r, 0, 360, 1, 1);
  for (int i = 0; i < count; i++) {
    float angle = (float)i * 360.0f / (float)count;
    tick(cx, cy, r - 6, r - 1, angle, 1);
  }
}

static void gauge(int cx, int cy, float value, float min, float max, const char *label, const char *unit) {
  // arc background
  al_arc(cx, cy, GR, GA_START, GA_END, 1, 2);

  // tick marks
  for (int i = 0; i <= 4; i++) {
    float angle = (float)GA_START + (float)i * (float)(GA_END - GA_START) / 4.0f;
    tick(cx, cy, GR - 4, GR - 1, angle, 1);
  }

  // needle
  float frac = (value - min) / (max - min);
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  float needle_angle = (float)GA_START + frac * (float)(GA_END - GA_START);
  hand(cx, cy, GR - 3, 1, needle_angle);

  // label
  al_write(cx, cy - 10, 0, 8, 1, label, AL_WRITE_ALIGN_CENTER);

  // value
  char buf[16];
  snprintf(buf, sizeof(buf), "%s", unit);
  al_write(cx, cy + 3, 0, 8, 1, buf, AL_WRITE_ALIGN_CENTER);
}

int main() {
  bool running = false;
  int64_t elapsed = 0;
  int64_t start_time = 0;

  for (;;) {
    // clear screen
    al_clear(0);

    // get current time and date
    int hour = al_clock(0, AL_CLOCK_HOUR);
    int minute = al_clock(0, AL_CLOCK_MINUTE);
    int second = al_clock(0, AL_CLOCK_SECOND);
    int year = al_clock(0, AL_CLOCK_YEAR);
    int month = al_clock(0, AL_CLOCK_MONTH);
    int day = al_clock(0, AL_CLOCK_DAY);

    // get sensor values
    float temp = al_info(AL_INFO_SENSOR_TEMPERATURE);
    float hum = al_info(AL_INFO_SENSOR_HUMIDITY);
    float co2 = al_info(AL_INFO_SENSOR_CO2);
    float voc = al_info(AL_INFO_SENSOR_VOC);
    float nox = al_info(AL_INFO_SENSOR_NOX);
    float prs = al_info(AL_INFO_SENSOR_PRESSURE);

    // compute day of week (Tomohiko Sakamoto)
    int dow_y = year;
    int dow_t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) dow_y--;
    int dow = (dow_y + dow_y / 4 - dow_y / 100 + dow_y / 400 + dow_t[month - 1] + day) % 7;

    // compute timer
    int64_t timer_ms = elapsed;
    if (running) {
      timer_ms += al_millis() - start_time;
    }
    int timer_sec = (int)(timer_ms / 1000) % 60;
    int timer_min = (int)(timer_ms / 60000) % 60;

    // === left gauges: temp, humidity, co2 ===
    char val[16];

    snprintf(val, sizeof(val), "%.0f", temp);
    gauge(30, 22, temp, -10.0f, 50.0f, "TMP", val);

    snprintf(val, sizeof(val), "%.0f", hum);
    gauge(30, 64, hum, 0.0f, 100.0f, "HUM", val);

    snprintf(val, sizeof(val), "%.0f", co2);
    gauge(30, 106, co2, 400.0f, 2000.0f, "CO2", val);

    // === right gauges: voc, nox, pressure ===
    snprintf(val, sizeof(val), "%.0f", voc);
    gauge(AL_W - 30, 22, voc, 0.0f, 500.0f, "VOC", val);

    snprintf(val, sizeof(val), "%.0f", nox);
    gauge(AL_W - 30, 64, nox, 0.0f, 500.0f, "NOX", val);

    snprintf(val, sizeof(val), "%.0f", prs);
    gauge(AL_W - 30, 106, prs, 950.0f, 1050.0f, "PRS", val);

    // === main dial ===

    // outer ring
    al_arc(CX, CY, R, 0, 360, 1, 2);

    // hour tick marks
    for (int i = 0; i < 12; i++) {
      float angle = (float)i * 30.0f;
      tick(CX, CY, R - 5, R - 1, angle, 2);
    }

    // === subdials ===

    // left: day of week (7 positions)
    subdial_ring(SUB_LX, SUB_LY, SR, 7);
    hand(SUB_LX, SUB_LY, SR - 6, 1, (float)dow * 360.0f / 7.0f);
    al_rect(SUB_LX - 1, SUB_LY - 1, 3, 3, 1, 0);

    // right: date (31 positions)
    subdial_ring(SUB_RX, SUB_RY, SR, 31);
    hand(SUB_RX, SUB_RY, SR - 6, 1, (float)(day - 1) * 360.0f / 31.0f);
    al_rect(SUB_RX - 1, SUB_RY - 1, 3, 3, 1, 0);

    // bottom: timer (60 positions, shows seconds)
    subdial_ring(SUB_BX, SUB_BY, SR, 12);
    hand(SUB_BX, SUB_BY, SR - 6, 1, (float)timer_sec * 6.0f);
    al_rect(SUB_BX - 1, SUB_BY - 1, 3, 3, 1, 0);

    // timer minutes display
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", timer_min, timer_sec);
    al_write(CX, CY - 22, 0, 8, 1, buf, AL_WRITE_ALIGN_CENTER);

    // === main hands ===
    float h_angle = (float)(hour % 12) * 30.0f + (float)minute * 0.5f;
    float m_angle = (float)minute * 6.0f + (float)second * 0.1f;
    float s_angle = (float)second * 6.0f;

    hand(CX, CY, R - 26, 3, h_angle);
    hand(CX, CY, R - 14, 2, m_angle);
    hand(CX, CY, R - 10, 1, s_angle);

    // center dot
    al_rect(CX - 2, CY - 2, 5, 5, 1, 0);

    // wait and check for button
    al_yield_result_t ev = al_yield(200, 0);
    if (ev == AL_YIELD_ENTER) {
      if (running) {
        elapsed += al_millis() - start_time;
        running = false;
      } else {
        start_time = al_millis();
        running = true;
      }
    } else if (ev == AL_YIELD_ESCAPE) {
      return 0;
    }
  }
}
