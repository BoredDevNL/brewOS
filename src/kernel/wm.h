#ifndef WM_H
#define WM_H

#include <stdint.h>
#include <stdbool.h>

// --- Constants ---
#define COLOR_TEAL      0xFF008080
#define COLOR_GRAY      0xFFC0C0C0
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_BLACK     0xFF000000
#define COLOR_BLUE      0xFF000080
#define COLOR_LTGRAY    0xFFDFDFDF
#define COLOR_DKGRAY    0xFF808080
#define COLOR_RED       0xFFFF0000
#define COLOR_COFFEE    0xFF6B4423
#define COLOR_APPLE_RED    0xFFFF0000
#define COLOR_APPLE_ORANGE 0xFFFF7F00
#define COLOR_APPLE_YELLOW 0xFFFFFF00
#define COLOR_APPLE_GREEN  0xFF00FF00
#define COLOR_APPLE_BLUE   0xFF0000FF
#define COLOR_APPLE_INDIGO 0xFF4B0082
#define COLOR_APPLE_VIOLET 0xFF9400D3

typedef struct Window Window;
struct Window {
    char *title;
    int x, y, w, h;
    bool visible;
    char buffer[1024];
    int buf_len;
    int cursor_pos;
    bool focused;
    int z_index;  // Layering depth (higher = on top)
    
    // Callbacks
    void (*paint)(Window *win);
    void (*handle_key)(Window *win, char c);
    void (*handle_click)(Window *win, int x, int y);
    void (*handle_right_click)(Window *win, int x, int y);
};

void wm_init(void);
void wm_handle_mouse(int dx, int dy, uint8_t buttons);
void wm_handle_key(char c);
void wm_handle_click(int x, int y);
void wm_handle_right_click(int x, int y);

// Redraw system
void wm_mark_dirty(int x, int y, int w, int h);
void wm_refresh(void);
void wm_paint(void);
void wm_timer_tick(void);

// Hook for external rendering (e.g. VM overlay)
extern void (*wm_custom_paint_hook)(void);

// Drawing helpers
void draw_bevel_rect(int x, int y, int w, int h, bool sunken);
void draw_button(int x, int y, int w, int h, const char *text, bool pressed);

#endif
