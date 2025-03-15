#ifndef GFX_H
#define GFX_H

#include <lvgl.h>

void gfx_init();

void gfx_begin(bool refresh, bool invert);
void gfx_end(bool skip, bool wait);

lv_group_t* gfx_get_group();

#endif  // GFX_H
