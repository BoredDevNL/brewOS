#include "about.h"
#include "graphics.h"
#include "wm.h"
#include <stddef.h>

Window win_about;

// Color definitions
#define COLOR_BLUE_LOGO     0xFF1E8AF5
#define COLOR_GREEN_LOGO    0xFF6DD651
#define COLOR_YELLOW_LOGO   0xFFF5BE34
#define COLOR_RED_LOGO      0xFFF05456
#define COLOR_PURPLE_LOGO   0xFFA65DC2
#define COLOR_CYAN_LOGO     0xFF368DF7

static void about_paint(Window *win) {
    // Background
    draw_rect(win->x + 4, win->y + 24, win->w - 8, win->h - 28, COLOR_LTGRAY);
    
    int offset_x = win->x + 15;
    int offset_y = win->y + 35;
    
    // Draw brewkernel ASCII logo
    draw_string(offset_x, offset_y, "( (", COLOR_BLUE_LOGO);
    
    draw_string(offset_x, offset_y + 15, "    ) )", COLOR_GREEN_LOGO);
    
    draw_string(offset_x, offset_y + 30, "  ........", COLOR_YELLOW_LOGO);
    
    draw_string(offset_x, offset_y + 45, "  |      |]", COLOR_RED_LOGO);
    
    draw_string(offset_x, offset_y + 60, "  \\      /", COLOR_PURPLE_LOGO);
    
    draw_string(offset_x, offset_y + 75, "   `----'", COLOR_CYAN_LOGO);
    
    // Version info
    draw_string(offset_x, offset_y + 105, "BrewOS", COLOR_BLACK);
    draw_string(offset_x, offset_y + 120, "BrewOS Version 1.10", COLOR_BLACK);
    draw_string(offset_x, offset_y + 135, "Kernel Version 2.1.0", COLOR_BLACK);
    
    // Copyright
    draw_string(offset_x, offset_y + 150, "(C) 2026 boreddevnl.", COLOR_BLACK);
    draw_string(offset_x, offset_y + 165, "All rights reserved.", COLOR_BLACK);
}

static void about_click(Window *win, int x, int y) {
    (void)win;
    (void)x;
    (void)y;
    // No interactive elements needed for About dialog
}

void about_init(void) {
    win_about.title = "About BrewOS";
    win_about.x = 250;
    win_about.y = 180;
    win_about.w = 185;
    win_about.h = 240;
    win_about.visible = false;
    win_about.focused = false;
    win_about.z_index = 0;
    win_about.paint = about_paint;
    win_about.handle_click = about_click;
    win_about.handle_right_click = NULL;
    win_about.handle_key = NULL;
}
