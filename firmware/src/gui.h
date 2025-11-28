#ifndef GUI_H
#define GUI_H

#include <stdbool.h>

void gui_cleanup(bool refresh);

void gui_write(const char* text, bool wait);
void gui_message(const char* text, uint32_t timeout);

void gui_progress_start(const char* text);
void gui_progress_update(size_t current, size_t total);

bool gui_confirm(const char* message, const char* confirm, const char* cancel, bool invert, int64_t timeout);
int gui_choose(const char* first, const char* second, bool invert, int64_t timeout);

typedef struct {
  const char* title;
  const char* info;
} gui_list_item_t;

typedef gui_list_item_t (*gui_list_cb_t)(int num, void* ctx);

int gui_list(int total, int selected, int* offset, const char* select, const char* cancel, gui_list_cb_t cb, void* ctx,
             int64_t timeout);
int gui_list_strings(int start, int* offset, const char** strings, const char* select, const char* cancel,
                     int64_t timeout);

bool gui_wheel(const char* title, int32_t* value, int32_t min, int32_t step, int32_t max, const char* ok,
               const char* cancel, const char* format, int64_t timeout);

void gui_cycle(bool small, const char** texts, const char* next, const char* back);

#endif  // GUI_H
