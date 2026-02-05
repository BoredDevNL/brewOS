#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include <stdbool.h>
#include "limine.h"

// Dirty rectangle structure
typedef struct {
    int x, y, w, h;
    bool active;
} DirtyRect;

void graphics_init(struct limine_framebuffer *fb);
void put_pixel(int x, int y, uint32_t color);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void draw_char(int x, int y, char c, uint32_t color);
void draw_string(int x, int y, const char *s, uint32_t color);
void draw_desktop_background(void);
void graphics_set_bg_color(uint32_t color);
void graphics_set_bg_pattern(const uint32_t *pattern);  // 128x128 pattern

// Get screen dimensions
int get_screen_width(void);
int get_screen_height(void);

// Dirty rectangle management
void graphics_mark_dirty(int x, int y, int w, int h);
void graphics_mark_screen_dirty(void);
DirtyRect graphics_get_dirty_rect(void);
void graphics_clear_dirty(void);

// Double buffering
void graphics_flip_buffer(void);
void graphics_clear_back_buffer(uint32_t color);

#endif
