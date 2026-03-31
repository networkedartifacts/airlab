#include "../common.h"

#define GLYPH_COUNT 12
#define SPACING 2

typedef struct {
  char ch;
  int sprite;
} glyph_t;

static glyph_t glyphs[GLYPH_COUNT];

static void init_glyphs(const char *font) {
  const char *suffixes[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "dot", "minus"};
  const char chars[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '-'};
  for (int i = 0; i < GLYPH_COUNT; i++) {
    char name[64];
    snprintf(name, sizeof(name), "%s/%s", font, suffixes[i]);
    glyphs[i].ch = chars[i];
    glyphs[i].sprite = al_sprite_resolve(name);
  }
}

static int find_glyph(char c) {
  for (int i = 0; i < GLYPH_COUNT; i++) {
    if (glyphs[i].ch == c) return i;
  }
  return -1;
}

static int measure(const char *str) {
  int w = 0;
  for (const char *p = str; *p; p++) {
    int i = find_glyph(*p);
    if (i >= 0) w += al_sprite_width(glyphs[i].sprite) + SPACING;
  }
  return w > 0 ? w - SPACING : 0;
}

static int max_height(const char *str) {
  int h = 0;
  for (const char *p = str; *p; p++) {
    int i = find_glyph(*p);
    if (i >= 0) {
      int sh = al_sprite_height(glyphs[i].sprite);
      if (sh > h) h = sh;
    }
  }
  return h;
}

static void draw_str(int x, int y, const char *str) {
  for (const char *p = str; *p; p++) {
    int i = find_glyph(*p);
    if (i >= 0) {
      al_sprite_draw(glyphs[i].sprite, x, y, 1, 0);
      x += al_sprite_width(glyphs[i].sprite) + SPACING;
    }
  }
}

int main() {
  // patch temperature
  al_patch_temp(&al_sensors[AL_SENSOR_TMP]);

  // resolve sensor
  int sidx = al_find_sensor("sensor", "co2");
  al_sensor_t *s = &al_sensors[sidx];

  // load font glyphs
  char font[32] = {0};
  al_config_get_s("font", font, sizeof(font));
  if (strlen(font) == 0) strcpy(font, "comic");
  init_glyphs(font);

  // format current value
  float val = al_sensor_value(s->info);
  char buf[16];
  snprintf(buf, sizeof(buf), s->fmt, val);

  // compute centered layout
  int str_w = measure(buf);
  int glyph_h = max_height(buf);
  int show_unit = al_config_get_b("unit");
  int total_h = show_unit ? glyph_h + 4 + 16 : glyph_h;
  int y_start = (AL_H - total_h) / 2;

  // clear screen and draw value
  al_clear(0);
  draw_str((AL_W - str_w) / 2, y_start, buf);

  // draw unit label below value
  if (show_unit) {
    al_write(AL_W / 2, AL_H - 32, 0, 16, 1, s->unit, AL_WRITE_ALIGN_CENTER);
  }

  return 0;
}
