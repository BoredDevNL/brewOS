#include <stddef.h>
#include "graphics.h"
#include "font.h"

static struct limine_framebuffer *g_fb = NULL;
static uint32_t g_bg_color = 0xFF6B4423;  // Coffee color by default

// Dirty rectangle tracking
static DirtyRect g_dirty = {0, 0, 0, 0, false};

// Double buffering - allocate a back buffer
// Max screen size: 2048x2048 @ 32bpp = 16MB, but we'll allocate for common sizes
// Using a simple approach: allocate max size buffer
#define MAX_FB_WIDTH 2048
#define MAX_FB_HEIGHT 2048
static uint32_t g_back_buffer[MAX_FB_WIDTH * MAX_FB_HEIGHT] __attribute__((aligned(4096)));

void graphics_init(struct limine_framebuffer *fb) {
    g_fb = fb;
    g_dirty.active = false;
    // Initialize back buffer to black
    for (int i = 0; i < MAX_FB_WIDTH * MAX_FB_HEIGHT; i++) {
        g_back_buffer[i] = 0;
    }
}

int get_screen_width(void) {
    return g_fb ? g_fb->width : 0;
}

int get_screen_height(void) {
    return g_fb ? g_fb->height : 0;
}

// Merge new dirty rect with existing one
static void merge_dirty_rect(int x, int y, int w, int h) {
    if (!g_dirty.active) {
        g_dirty.x = x;
        g_dirty.y = y;
        g_dirty.w = w;
        g_dirty.h = h;
        g_dirty.active = true;
    } else {
        // Calculate union of two rectangles
        int x1 = g_dirty.x;
        int y1 = g_dirty.y;
        int x2 = g_dirty.x + g_dirty.w;
        int y2 = g_dirty.y + g_dirty.h;
        
        int new_x1 = x;
        int new_y1 = y;
        int new_x2 = x + w;
        int new_y2 = y + h;
        
        g_dirty.x = new_x1 < x1 ? new_x1 : x1;
        g_dirty.y = new_y1 < y1 ? new_y1 : y1;
        g_dirty.w = (new_x2 > x2 ? new_x2 : x2) - g_dirty.x;
        g_dirty.h = (new_y2 > y2 ? new_y2 : y2) - g_dirty.y;
    }
}

void graphics_mark_dirty(int x, int y, int w, int h) {
    // Clamp to screen bounds
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > get_screen_width()) {
        w = get_screen_width() - x;
    }
    if (y + h > get_screen_height()) {
        h = get_screen_height() - y;
    }
    
    if (w <= 0 || h <= 0) return;
    
    merge_dirty_rect(x, y, w, h);
}

void graphics_mark_screen_dirty(void) {
    g_dirty.x = 0;
    g_dirty.y = 0;
    g_dirty.w = get_screen_width();
    g_dirty.h = get_screen_height();
    g_dirty.active = true;
}

DirtyRect graphics_get_dirty_rect(void) {
    return g_dirty;
}

void graphics_clear_dirty(void) {
    g_dirty.active = false;
}

void put_pixel(int x, int y, uint32_t color) {
    if (!g_fb) return;
    if (x < 0 || x >= (int)g_fb->width || y < 0 || y >= (int)g_fb->height) return;
    
    // Draw to back buffer
    uint32_t pixel_offset = y * g_fb->width + x;
    g_back_buffer[pixel_offset] = color;
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            put_pixel(x + j, y + i, color);
        }
    }
}

void draw_char(int x, int y, char c, uint32_t color) {
    unsigned char uc = (unsigned char)c;
    if (uc > 127) return;
    const uint8_t *glyph = font8x8_basic[uc];

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if ((glyph[row] >> (7 - col)) & 1) {
                put_pixel(x + col, y + row, color);
            }
        }
    }
}

void draw_string(int x, int y, const char *s, uint32_t color) {
    int cur_x = x;
    int cur_y = y;
    while (*s) {
        if (*s == '\n') {
            cur_x = x;
            cur_y += 10;
        } else {
            draw_char(cur_x, cur_y, *s, color);
            cur_x += 8;
        }
        s++;
    }
}

void draw_desktop_background(void) {
    if (!g_fb) return;
    draw_rect(0, 0, g_fb->width, g_fb->height, g_bg_color);
}

void graphics_set_bg_color(uint32_t color) {
    g_bg_color = color;
}

// Double buffering functions
void graphics_clear_back_buffer(uint32_t color) {
    if (!g_fb) return;
    uint32_t *buf = g_back_buffer;
    for (int i = 0; i < (int)g_fb->width * (int)g_fb->height; i++) {
        *buf++ = color;
    }
}

void graphics_flip_buffer(void) {
    if (!g_fb) return;
    
    // Copy back buffer to framebuffer
    uint32_t *src = g_back_buffer;
    uint8_t *dst = (uint8_t *)g_fb->address;
    
    for (int y = 0; y < (int)g_fb->height; y++) {
        // Copy one scanline
        uint32_t *dst_row = (uint32_t *)dst;
        for (int x = 0; x < (int)g_fb->width; x++) {
            dst_row[x] = src[x];
        }
        src += g_fb->width;
        dst += g_fb->pitch;
    }
}
