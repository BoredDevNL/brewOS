#include "control_panel.h"
#include "graphics.h"
#include <stddef.h>
#include "wm.h"
#include "network.h"

Window win_control_panel;

#define COLOR_COFFEE    0xFF6B4423
#define COLOR_TEAL      0xFF008080
#define COLOR_GREEN     0xFF008000
#define COLOR_BLUE_BG   0xFF000080
#define COLOR_PURPLE    0xFF800080
#define COLOR_GREY      0xFF454545

// Control panel state
#define VIEW_MAIN 0
#define VIEW_WALLPAPER 1
#define VIEW_NETWORK 2

static int current_view = VIEW_MAIN;
static char rgb_r[4] = "";
static char rgb_g[4] = "";
static char rgb_b[4] = "";
static int focused_field = -1;
static int input_cursor = 0;

// Network panel state
static char ip_1[4] = "";
static char ip_2[4] = "";
static char ip_3[4] = "";
static char ip_4[4] = "";
static char dest_ip_1[4] = "";
static char dest_ip_2[4] = "";
static char dest_ip_3[4] = "";
static char dest_ip_4[4] = "";
static char udp_port[6] = "";
static char udp_message[128] = "";
static char net_status[64] = "";

// Pattern buffers (128x128)
#define PATTERN_SIZE 128
static uint32_t pattern_lumberjack[PATTERN_SIZE * PATTERN_SIZE];
static uint32_t pattern_blue_diamond[PATTERN_SIZE * PATTERN_SIZE];

static void generate_lumberjack_pattern(void) {
    // Lumberjack pattern: 3x3 repeating cell
    // Red corners, dark grey cross (top/left/right/bottom), black center
    uint32_t red = 0xFFDC143C;
    uint32_t dark_grey = 0xFF404040;
    uint32_t black = 0xFF000000;
    
    // Fill entire pattern with the 3x3 repeating cell
    for (int y = 0; y < PATTERN_SIZE; y++) {
        for (int x = 0; x < PATTERN_SIZE; x++) {
            int cell_x = x % 3;
            int cell_y = y % 3;
            
            uint32_t color;
            
            // Determine color based on position in 3x3 cell
            if (cell_x == 1 && cell_y == 1) {
                // Center: black
                color = black;
            } else if (cell_x == 1 || cell_y == 1) {
                // Cross (top, left, right, bottom): dark grey
                color = dark_grey;
            } else {
                // Corners: red
                color = red;
            }
            
            pattern_lumberjack[y * PATTERN_SIZE + x] = color;
        }
    }
}

static void generate_blue_diamond_pattern(void) {
    // Blue diamond pattern on light blue background
    uint32_t bg_color = 0xFFADD8E6;  // Light blue
    uint32_t diamond_color = 0xFF0000CD;  // Medium blue
    
    for (int y = 0; y < PATTERN_SIZE; y++) {
        for (int x = 0; x < PATTERN_SIZE; x++) {
            pattern_blue_diamond[y * PATTERN_SIZE + x] = bg_color;
        }
    }
    
    // Draw diamonds (centered at 32, 32 and 96, 96)
    for (int dy = -24; dy <= 24; dy++) {
        for (int dx = -24; dx <= 24; dx++) {
            // Diamond shape: |dx| + |dy| <= 24
            int abs_dx = dx < 0 ? -dx : dx;
            int abs_dy = dy < 0 ? -dy : dy;
            if (abs_dx + abs_dy <= 24) {
                // Top-left diamond
                int x1 = 32 + dx;
                int y1 = 32 + dy;
                if (x1 >= 0 && x1 < PATTERN_SIZE && y1 >= 0 && y1 < PATTERN_SIZE) {
                    pattern_blue_diamond[y1 * PATTERN_SIZE + x1] = diamond_color;
                }
                
                // Bottom-right diamond
                int x2 = 96 + dx;
                int y2 = 96 + dy;
                if (x2 >= 0 && x2 < PATTERN_SIZE && y2 >= 0 && y2 < PATTERN_SIZE) {
                    pattern_blue_diamond[y2 * PATTERN_SIZE + x2] = diamond_color;
                }
            }
        }
    }
}

static uint32_t parse_rgb_separate(const char *r, const char *g, const char *b) {
    int rv = 0, gv = 0, bv = 0;
    
    // Parse R
    for (int i = 0; r[i] && i < 3; i++) {
        if (r[i] >= '0' && r[i] <= '9') {
            rv = rv * 10 + (r[i] - '0');
        }
    }
    
    // Parse G
    for (int i = 0; g[i] && i < 3; i++) {
        if (g[i] >= '0' && g[i] <= '9') {
            gv = gv * 10 + (g[i] - '0');
        }
    }
    
    // Parse B
    for (int i = 0; b[i] && i < 3; i++) {
        if (b[i] >= '0' && b[i] <= '9') {
            bv = bv * 10 + (b[i] - '0');
        }
    }
    
    // Clamp values
    if (rv > 255) rv = 255;
    if (gv > 255) gv = 255;
    if (bv > 255) bv = 255;
    
    return 0xFF000000 | (rv << 16) | (gv << 8) | bv;
}

static void control_panel_paint_main(Window *win) {
    int offset_x = win->x + 8;
    int offset_y = win->y + 30;
    
    // Draw wallpaper painting icon
    // Frame
    draw_rect(offset_x + 5, offset_y + 2, 28, 20, 0xFF8B4513); // Brown frame
    draw_rect(offset_x + 6, offset_y + 3, 26, 18, 0xFFFFFFFF); // White canvas
    
    // Paint strokes (simple landscape)
    draw_rect(offset_x + 8, offset_y + 5, 22, 7, 0xFF87CEEB); // Sky blue
    draw_rect(offset_x + 8, offset_y + 12, 22, 5, 0xFF90EE90); // Light green grass
    draw_rect(offset_x + 15, offset_y + 8, 3, 4, 0xFF8B4513); // Tree trunk
    draw_rect(offset_x + 13, offset_y + 5, 7, 4, 0xFF228B22); // Tree leaves
    draw_rect(offset_x + 24, offset_y + 6, 4, 3, 0xFFFFFF00); // Sun
    
    draw_string(offset_x + 40, offset_y + 8, "Wallpaper", 0xFF000000);
    
    // Draw network globe icon
    int net_offset_y = offset_y + 35;
    
    // Globe circle (approximate with rectangles for filled circle)
    uint32_t globe_color = 0xFF4169E1; // Royal blue
    
    // Draw filled circle using rectangles (simplified)
    draw_rect(offset_x + 11, net_offset_y + 3, 12, 1, globe_color);
    draw_rect(offset_x + 9, net_offset_y + 4, 16, 1, globe_color);
    draw_rect(offset_x + 8, net_offset_y + 5, 18, 1, globe_color);
    draw_rect(offset_x + 7, net_offset_y + 6, 20, 1, globe_color);
    draw_rect(offset_x + 6, net_offset_y + 7, 22, 1, globe_color);
    draw_rect(offset_x + 6, net_offset_y + 8, 22, 1, globe_color);
    draw_rect(offset_x + 6, net_offset_y + 9, 22, 1, globe_color);
    draw_rect(offset_x + 6, net_offset_y + 10, 22, 1, globe_color);
    draw_rect(offset_x + 6, net_offset_y + 11, 22, 1, globe_color);
    draw_rect(offset_x + 6, net_offset_y + 12, 22, 1, globe_color);
    draw_rect(offset_x + 6, net_offset_y + 13, 22, 1, globe_color);
    draw_rect(offset_x + 7, net_offset_y + 14, 20, 1, globe_color);
    draw_rect(offset_x + 8, net_offset_y + 15, 18, 1, globe_color);
    draw_rect(offset_x + 9, net_offset_y + 16, 16, 1, globe_color);
    draw_rect(offset_x + 11, net_offset_y + 17, 12, 1, globe_color);
    
    // Latitude lines (white)
    draw_rect(offset_x + 7, net_offset_y + 8, 20, 1, 0xFFFFFFFF);
    draw_rect(offset_x + 7, net_offset_y + 12, 20, 1, 0xFFFFFFFF);
    
    // Longitude line (vertical, white)
    draw_rect(offset_x + 17, net_offset_y + 6, 1, 9, 0xFFFFFFFF);
    
    // Curved longitude lines
    draw_rect(offset_x + 11, net_offset_y + 5, 1, 11, 0xFFFFFFFF);
    draw_rect(offset_x + 23, net_offset_y + 5, 1, 11, 0xFFFFFFFF);
    
    draw_string(offset_x + 40, net_offset_y + 8, "Network", 0xFF000000);
}

static void control_panel_paint_wallpaper(Window *win) {
    int offset_x = win->x + 8;
    int offset_y = win->y + 30;
    
    // Back button
    draw_string(offset_x, offset_y, "< Back", 0xFF000080);
    
    draw_string(offset_x, offset_y + 25, "Presets:", 0xFF000000);
    
    // Color buttons
    int button_y = offset_y + 45;
    int button_x = offset_x;
    
    // Coffee button
    draw_button(button_x, button_y, 60, 20, "Coffee", false);
    draw_rect(button_x + 65, button_y + 5, 20, 10, COLOR_COFFEE);
    
    // Teal button
    draw_button(button_x + 100, button_y, 60, 20, "Teal", false);
    draw_rect(button_x + 165, button_y + 5, 20, 10, COLOR_TEAL);
    
    // Green button
    draw_button(button_x + 200, button_y, 60, 20, "Green", false);
    draw_rect(button_x + 265, button_y + 5, 20, 10, COLOR_GREEN);
    
    // Blue button
    button_y += 30;
    draw_button(button_x, button_y, 60, 20, "Blue", false);
    draw_rect(button_x + 65, button_y + 5, 20, 10, COLOR_BLUE_BG);
    
    // Purple button
    draw_button(button_x + 100, button_y, 60, 20, "Purple", false);
    draw_rect(button_x + 165, button_y + 5, 20, 10, COLOR_PURPLE);
    
    // Grey button
    draw_button(button_x + 200, button_y, 60, 20, "Grey", false);
    draw_rect(button_x + 265, button_y + 5, 20, 10, COLOR_GREY);
    
    // Pattern section
    button_y += 40;
    draw_string(offset_x, button_y, "Patterns:", 0xFF000000);
    
    button_y += 20;
    
    // Lumberjack pattern button
    draw_button(button_x, button_y, 100, 20, "Lumberjack", false);
    // Draw small pattern preview (3x3 repeating)
    for (int py = 0; py < 12; py++) {
        for (int px = 0; px < 18; px++) {
            int cell_x = px % 3;
            int cell_y = py % 3;
            uint32_t color;
            
            if (cell_x == 1 && cell_y == 1) {
                color = 0xFF000000; // Black center
            } else if (cell_x == 1 || cell_y == 1) {
                color = 0xFF404040; // Dark grey cross
            } else {
                color = 0xFFDC143C; // Red corners
            }
            
            draw_rect(button_x + 110 + px, button_y + 4 + py, 1, 1, color);
        }
    }
    
    // Blue Diamond pattern button
    draw_button(button_x + 145, button_y, 115, 20, "Blue Diamond", false);
    // Draw small diamond preview
    for (int py = 0; py < 10; py++) {
        for (int px = 0; px < 20; px++) {
            int cx = px - 10;
            int cy = py - 5;
            int abs_cx = cx < 0 ? -cx : cx;
            int abs_cy = cy < 0 ? -cy : cy;
            uint32_t color = (abs_cx + abs_cy <= 5) ? 0xFF0000CD : 0xFFADD8E6;
            draw_rect(button_x + 270 + px, button_y + 5 + py, 1, 1, color);
        }
    }
    
    // Custom color section
    button_y += 40;
    draw_string(offset_x, button_y, "Or something custom", 0xFF000000);
    
    button_y += 20;
    
    // R input box
    draw_string(button_x, button_y, "R:", 0xFF000000);
    draw_rect(button_x + 25, button_y, 50, 15, 0xFFFFFFFF);
    draw_rect(button_x + 25, button_y, 50, 1, COLOR_BLACK);
    draw_rect(button_x + 25, button_y, 1, 15, COLOR_BLACK);
    draw_rect(button_x + 74, button_y, 1, 15, COLOR_BLACK);
    draw_rect(button_x + 25, button_y + 14, 50, 1, COLOR_BLACK);
    draw_string(button_x + 30, button_y + 3, rgb_r, (focused_field == 0) ? 0xFFFF0000 : COLOR_BLACK);
    if (focused_field == 0) {
        // Draw cursor
        int cursor_x = button_x + 30 + input_cursor * 8;
        draw_rect(cursor_x, button_y + 3, 1, 9, 0xFFFF0000);
    }
    
    // G input box
    draw_string(button_x + 90, button_y, "G:", 0xFF000000);
    draw_rect(button_x + 115, button_y, 50, 15, 0xFFFFFFFF);
    draw_rect(button_x + 115, button_y, 50, 1, COLOR_BLACK);
    draw_rect(button_x + 115, button_y, 1, 15, COLOR_BLACK);
    draw_rect(button_x + 164, button_y, 1, 15, COLOR_BLACK);
    draw_rect(button_x + 115, button_y + 14, 50, 1, COLOR_BLACK);
    draw_string(button_x + 120, button_y + 3, rgb_g, (focused_field == 1) ? 0xFF00AA00 : COLOR_BLACK);
    if (focused_field == 1) {
        // Draw cursor
        int cursor_x = button_x + 120 + input_cursor * 8;
        draw_rect(cursor_x, button_y + 3, 1, 9, 0xFF00AA00);
    }
    
    // B input box
    draw_string(button_x + 180, button_y, "B:", 0xFF000000);
    draw_rect(button_x + 205, button_y, 50, 15, 0xFFFFFFFF);
    draw_rect(button_x + 205, button_y, 50, 1, COLOR_BLACK);
    draw_rect(button_x + 205, button_y, 1, 15, COLOR_BLACK);
    draw_rect(button_x + 254, button_y, 1, 15, COLOR_BLACK);
    draw_rect(button_x + 205, button_y + 14, 50, 1, COLOR_BLACK);
    draw_string(button_x + 210, button_y + 3, rgb_b, (focused_field == 2) ? 0xFF0000FF : COLOR_BLACK);
    if (focused_field == 2) {
        // Draw cursor
        int cursor_x = button_x + 210 + input_cursor * 8;
        draw_rect(cursor_x, button_y + 3, 1, 9, 0xFF0000FF);
    }
    
    // Apply button
    draw_button(button_x, button_y + 25, 70, 20, "Apply", false);
}

static void draw_input_box(int x, int y, int width, const char *text, bool focused, int cursor_pos) {
    // Draw box
    draw_rect(x, y, width, 18, 0xFFFFFFFF);
    draw_rect(x, y, width, 1, COLOR_BLACK);
    draw_rect(x, y, 1, 18, COLOR_BLACK);
    draw_rect(x + width - 1, y, 1, 18, COLOR_BLACK);
    draw_rect(x, y + 17, width, 1, COLOR_BLACK);
    
    // Draw text
    uint32_t text_color = focused ? 0xFF0000FF : COLOR_BLACK;
    draw_string(x + 3, y + 4, text, text_color);
    
    // Draw cursor if focused
    if (focused) {
        int cursor_x = x + 3 + cursor_pos * 8;
        draw_rect(cursor_x, y + 4, 1, 9, 0xFF0000FF);
    }
}

static void control_panel_paint_network(Window *win) {
    int offset_x = win->x + 8;
    int offset_y = win->y + 30;
    
    // Back button
    draw_string(offset_x, offset_y, "< Back", 0xFF000080);
    
    // Network Init Button
    draw_string(offset_x, offset_y + 25, "Network:", 0xFF000000);
    draw_button(offset_x, offset_y + 45, 100, 22, "Init Network", false);
    
    // Status message
    if (net_status[0] != '\0') {
        draw_string(offset_x + 110, offset_y + 50, net_status, 0xFF008000);
    }
    
    // Set IP Section
    int section_y = offset_y + 80;
    draw_string(offset_x, section_y, "Set Static IP:", 0xFF000000);
    
    section_y += 20;
    // IP input boxes (4 octets)
    draw_input_box(offset_x, section_y, 40, ip_1, focused_field == 0, input_cursor);
    draw_string(offset_x + 42, section_y + 4, ".", COLOR_BLACK);
    draw_input_box(offset_x + 50, section_y, 40, ip_2, focused_field == 1, input_cursor);
    draw_string(offset_x + 92, section_y + 4, ".", COLOR_BLACK);
    draw_input_box(offset_x + 100, section_y, 40, ip_3, focused_field == 2, input_cursor);
    draw_string(offset_x + 142, section_y + 4, ".", COLOR_BLACK);
    draw_input_box(offset_x + 150, section_y, 40, ip_4, focused_field == 3, input_cursor);
    
    // Apply IP button
    draw_button(offset_x + 200, section_y, 70, 18, "Apply", false);
    
    // Send UDP Section
    section_y += 35;
    draw_string(offset_x, section_y, "Send UDP Message:", 0xFF000000);
    
    section_y += 20;
    draw_string(offset_x, section_y + 4, "IP:", COLOR_BLACK);
    draw_input_box(offset_x + 25, section_y, 40, dest_ip_1, focused_field == 4, input_cursor);
    draw_string(offset_x + 67, section_y + 4, ".", COLOR_BLACK);
    draw_input_box(offset_x + 75, section_y, 40, dest_ip_2, focused_field == 5, input_cursor);
    draw_string(offset_x + 117, section_y + 4, ".", COLOR_BLACK);
    draw_input_box(offset_x + 125, section_y, 40, dest_ip_3, focused_field == 6, input_cursor);
    draw_string(offset_x + 167, section_y + 4, ".", COLOR_BLACK);
    draw_input_box(offset_x + 175, section_y, 40, dest_ip_4, focused_field == 7, input_cursor);
    
    section_y += 25;
    draw_string(offset_x, section_y + 4, "Port:", COLOR_BLACK);
    draw_input_box(offset_x + 40, section_y, 60, udp_port, focused_field == 8, input_cursor);
    
    section_y += 25;
    draw_string(offset_x, section_y + 4, "Msg:", COLOR_BLACK);
    draw_input_box(offset_x + 40, section_y, 260, udp_message, focused_field == 9, input_cursor);
    
    // Send button
    section_y += 25;
    draw_button(offset_x, section_y, 80, 22, "Send", false);
}

static void control_panel_paint(Window *win) {
    if (current_view == VIEW_MAIN) {
        control_panel_paint_main(win);
    } else if (current_view == VIEW_WALLPAPER) {
        control_panel_paint_wallpaper(win);
    } else if (current_view == VIEW_NETWORK) {
        control_panel_paint_network(win);
    }
}

static void control_panel_handle_click(Window *win, int x, int y) {
    (void)win;
    
    if (current_view == VIEW_MAIN) {
        int offset_x = 8;
        int offset_y = 30;
        
        // Check wallpaper button click (icon + text)
        if (x >= offset_x + 5 && x < offset_x + 120 &&
            y >= offset_y && y < offset_y + 25) {
            current_view = VIEW_WALLPAPER;
            focused_field = -1;
        }
        
        // Check network button click (icon + text)
        int net_offset_y = offset_y + 35;
        if (x >= offset_x + 5 && x < offset_x + 120 &&
            y >= net_offset_y && y < net_offset_y + 25) {
            current_view = VIEW_NETWORK;
            focused_field = -1;
        }
    } else if (current_view == VIEW_WALLPAPER) {
        int offset_x = 8;
        int offset_y = 30;
        int button_y = offset_y + 45;
        int button_x = offset_x;
        
        // Back button
        if (x >= offset_x && x < offset_x + 40 &&
            y >= offset_y && y < offset_y + 15) {
            current_view = VIEW_MAIN;
            return;
        }
        
        // Check Coffee button
        if (x >= button_x && x < button_x + 60 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_color(COLOR_COFFEE);
            return;
        }
        
        // Check Teal button
        if (x >= button_x + 100 && x < button_x + 160 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_color(COLOR_TEAL);
            return;
        }
        
        // Check Green button
        if (x >= button_x + 200 && x < button_x + 260 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_color(COLOR_GREEN);
            return;
        }
        
        // Check Blue button
        button_y += 30;
        if (x >= button_x && x < button_x + 60 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_color(COLOR_BLUE_BG);
            return;
        }
        
        // Check Purple button
        if (x >= button_x + 100 && x < button_x + 160 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_color(COLOR_PURPLE);
            return;
        }
        
        // Check Grey button
        if (x >= button_x + 200 && x < button_x + 260 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_color(COLOR_GREY);
            return;
        }
        
        // Pattern section
        button_y += 40;
        button_y += 20;
        
        // Check Lumberjack pattern button
        if (x >= button_x && x < button_x + 100 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_pattern(pattern_lumberjack);
            return;
        }
        
        // Check Blue Diamond pattern button
        if (x >= button_x + 145 && x < button_x + 260 && y >= button_y && y < button_y + 20) {
            graphics_set_bg_pattern(pattern_blue_diamond);
            return;
        }
        
        // Custom RGB section
        button_y += 40;
        button_y += 20;
        
        // Check R input box click
        if (x >= button_x + 25 && x < button_x + 75 && y >= button_y && y < button_y + 15) {
            if (focused_field != 0) {
                rgb_r[0] = '\0';  // Clear when first focused
            }
            focused_field = 0;
            input_cursor = 0;
            return;
        }
        
        // Check G input box click
        if (x >= button_x + 115 && x < button_x + 165 && y >= button_y && y < button_y + 15) {
            if (focused_field != 1) {
                rgb_g[0] = '\0';  // Clear when first focused
            }
            focused_field = 1;
            input_cursor = 0;
            return;
        }
        
        // Check B input box click
        if (x >= button_x + 205 && x < button_x + 255 && y >= button_y && y < button_y + 15) {
            if (focused_field != 2) {
                rgb_b[0] = '\0';  // Clear when first focused
            }
            focused_field = 2;
            input_cursor = 0;
            return;
        }
        
        // Check Apply button
        if (x >= button_x && x < button_x + 70 && y >= button_y + 25 && y < button_y + 45) {
            graphics_set_bg_color(parse_rgb_separate(rgb_r, rgb_g, rgb_b));
            return;
        }
    } else if (current_view == VIEW_NETWORK) {
        int offset_x = 8;
        int offset_y = 30;
        
        // Back button
        if (x >= offset_x && x < offset_x + 40 && y >= offset_y && y < offset_y + 15) {
            current_view = VIEW_MAIN;
            focused_field = -1;
            return;
        }
        
        // Init Network button
        if (x >= offset_x && x < offset_x + 100 && y >= offset_y + 45 && y < offset_y + 67) {
            int result = network_init();
            if (result == 0) {
                net_status[0] = 'I'; net_status[1] = 'n'; net_status[2] = 'i'; 
                net_status[3] = 't'; net_status[4] = 'e'; net_status[5] = 'd';
                net_status[6] = '\0';
            } else {
                net_status[0] = 'F'; net_status[1] = 'a'; net_status[2] = 'i'; 
                net_status[3] = 'l'; net_status[4] = 'e'; net_status[5] = 'd';
                net_status[6] = '\0';
            }
            return;
        }
        
        int section_y = offset_y + 80 + 20;
        
        // IP octet 1
        if (x >= offset_x && x < offset_x + 40 && y >= section_y && y < section_y + 18) {
            focused_field = 0;
            input_cursor = 0;
            return;
        }
        // IP octet 2
        if (x >= offset_x + 50 && x < offset_x + 90 && y >= section_y && y < section_y + 18) {
            focused_field = 1;
            input_cursor = 0;
            return;
        }
        // IP octet 3
        if (x >= offset_x + 100 && x < offset_x + 140 && y >= section_y && y < section_y + 18) {
            focused_field = 2;
            input_cursor = 0;
            return;
        }
        // IP octet 4
        if (x >= offset_x + 150 && x < offset_x + 190 && y >= section_y && y < section_y + 18) {
            focused_field = 3;
            input_cursor = 0;
            return;
        }
        
        // Apply IP button
        if (x >= offset_x + 200 && x < offset_x + 270 && y >= section_y && y < section_y + 18) {
            ipv4_address_t ip;
            ip.bytes[0] = 0; ip.bytes[1] = 0; ip.bytes[2] = 0; ip.bytes[3] = 0;
            
            // Parse IP octets
            for (int i = 0; ip_1[i] && i < 3; i++) {
                if (ip_1[i] >= '0' && ip_1[i] <= '9') {
                    ip.bytes[0] = ip.bytes[0] * 10 + (ip_1[i] - '0');
                }
            }
            for (int i = 0; ip_2[i] && i < 3; i++) {
                if (ip_2[i] >= '0' && ip_2[i] <= '9') {
                    ip.bytes[1] = ip.bytes[1] * 10 + (ip_2[i] - '0');
                }
            }
            for (int i = 0; ip_3[i] && i < 3; i++) {
                if (ip_3[i] >= '0' && ip_3[i] <= '9') {
                    ip.bytes[2] = ip.bytes[2] * 10 + (ip_3[i] - '0');
                }
            }
            for (int i = 0; ip_4[i] && i < 3; i++) {
                if (ip_4[i] >= '0' && ip_4[i] <= '9') {
                    ip.bytes[3] = ip.bytes[3] * 10 + (ip_4[i] - '0');
                }
            }
            
            network_set_ipv4_address(&ip);
            net_status[0] = 'I'; net_status[1] = 'P'; net_status[2] = ' ';
            net_status[3] = 's'; net_status[4] = 'e'; net_status[5] = 't';
            net_status[6] = '\0';
            return;
        }
        
        section_y += 35 + 20;
        
        // Dest IP octets
        if (x >= offset_x + 25 && x < offset_x + 65 && y >= section_y && y < section_y + 18) {
            focused_field = 4;
            input_cursor = 0;
            return;
        }
        if (x >= offset_x + 75 && x < offset_x + 115 && y >= section_y && y < section_y + 18) {
            focused_field = 5;
            input_cursor = 0;
            return;
        }
        if (x >= offset_x + 125 && x < offset_x + 165 && y >= section_y && y < section_y + 18) {
            focused_field = 6;
            input_cursor = 0;
            return;
        }
        if (x >= offset_x + 175 && x < offset_x + 215 && y >= section_y && y < section_y + 18) {
            focused_field = 7;
            input_cursor = 0;
            return;
        }
        
        section_y += 25;
        
        // Port field
        if (x >= offset_x + 40 && x < offset_x + 100 && y >= section_y && y < section_y + 18) {
            focused_field = 8;
            input_cursor = 0;
            return;
        }
        
        section_y += 25;
        
        // Message field
        if (x >= offset_x + 40 && x < offset_x + 300 && y >= section_y && y < section_y + 18) {
            focused_field = 9;
            input_cursor = 0;
            return;
        }
        
        section_y += 25;
        
        // Send button
        if (x >= offset_x && x < offset_x + 80 && y >= section_y && y < section_y + 22) {
            ipv4_address_t dest_ip;
            dest_ip.bytes[0] = 0; dest_ip.bytes[1] = 0; dest_ip.bytes[2] = 0; dest_ip.bytes[3] = 0;
            
            // Parse dest IP
            for (int i = 0; dest_ip_1[i] && i < 3; i++) {
                if (dest_ip_1[i] >= '0' && dest_ip_1[i] <= '9') {
                    dest_ip.bytes[0] = dest_ip.bytes[0] * 10 + (dest_ip_1[i] - '0');
                }
            }
            for (int i = 0; dest_ip_2[i] && i < 3; i++) {
                if (dest_ip_2[i] >= '0' && dest_ip_2[i] <= '9') {
                    dest_ip.bytes[1] = dest_ip.bytes[1] * 10 + (dest_ip_2[i] - '0');
                }
            }
            for (int i = 0; dest_ip_3[i] && i < 3; i++) {
                if (dest_ip_3[i] >= '0' && dest_ip_3[i] <= '9') {
                    dest_ip.bytes[2] = dest_ip.bytes[2] * 10 + (dest_ip_3[i] - '0');
                }
            }
            for (int i = 0; dest_ip_4[i] && i < 3; i++) {
                if (dest_ip_4[i] >= '0' && dest_ip_4[i] <= '9') {
                    dest_ip.bytes[3] = dest_ip.bytes[3] * 10 + (dest_ip_4[i] - '0');
                }
            }
            
            // Parse port
            int port = 0;
            for (int i = 0; udp_port[i] && i < 5; i++) {
                if (udp_port[i] >= '0' && udp_port[i] <= '9') {
                    port = port * 10 + (udp_port[i] - '0');
                }
            }
            
            // Get message length
            int msg_len = 0;
            while (udp_message[msg_len] && msg_len < 127) msg_len++;
            
            if (msg_len > 0 && port > 0) {
                int result = udp_send_packet(&dest_ip, (uint16_t)port, 54321, udp_message, (size_t)msg_len);
                if (result == 0) {
                    net_status[0] = 'S'; net_status[1] = 'e'; net_status[2] = 'n';
                    net_status[3] = 't'; net_status[4] = '\0';
                } else {
                    net_status[0] = 'F'; net_status[1] = 'a'; net_status[2] = 'i';
                    net_status[3] = 'l'; net_status[4] = '\0';
                }
            }
            return;
        }
    }
}

static void control_panel_handle_key(Window *win, char c) {
    (void)win;
    
    if (focused_field < 0) return;
    
    if (current_view == VIEW_WALLPAPER) {
        // Get the currently focused field buffer
        char *focused_buffer = NULL;
        int max_len = 3;  // RGB values are 0-255, max 3 digits
        
        if (focused_field == 0) {
            focused_buffer = rgb_r;
        } else if (focused_field == 1) {
            focused_buffer = rgb_g;
        } else if (focused_field == 2) {
            focused_buffer = rgb_b;
        } else {
            return;
        }
        
        if (c == '\b') {  // Backspace
            if (input_cursor > 0) {
                input_cursor--;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (c >= '0' && c <= '9') {  // Digits only
            if (input_cursor < max_len) {
                focused_buffer[input_cursor] = c;
                input_cursor++;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (c == '\t') {  // Tab - switch to next field
            focused_field = (focused_field + 1) % 3;
            input_cursor = 0;
        }
    } else if (current_view == VIEW_NETWORK) {
        char *focused_buffer = NULL;
        int max_len = 3;  // Default for IP octets
        
        // Select the focused field
        if (focused_field == 0) {
            focused_buffer = ip_1;
        } else if (focused_field == 1) {
            focused_buffer = ip_2;
        } else if (focused_field == 2) {
            focused_buffer = ip_3;
        } else if (focused_field == 3) {
            focused_buffer = ip_4;
        } else if (focused_field == 4) {
            focused_buffer = dest_ip_1;
        } else if (focused_field == 5) {
            focused_buffer = dest_ip_2;
        } else if (focused_field == 6) {
            focused_buffer = dest_ip_3;
        } else if (focused_field == 7) {
            focused_buffer = dest_ip_4;
        } else if (focused_field == 8) {
            focused_buffer = udp_port;
            max_len = 5;  // Ports up to 65535
        } else if (focused_field == 9) {
            focused_buffer = udp_message;
            max_len = 127;  // Message max length
        } else {
            return;
        }
        
        if (c == '\b') {  // Backspace
            if (input_cursor > 0) {
                input_cursor--;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (focused_field <= 8 && c >= '0' && c <= '9') {  // Digits only for IP/port
            if (input_cursor < max_len) {
                focused_buffer[input_cursor] = c;
                input_cursor++;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (focused_field == 9 && c >= 32 && c <= 126) {  // All printable chars for message
            if (input_cursor < max_len) {
                focused_buffer[input_cursor] = c;
                input_cursor++;
                focused_buffer[input_cursor] = '\0';
            }
        } else if (c == '\t') {  // Tab - switch to next field
            focused_field = (focused_field + 1) % 10;
            input_cursor = 0;
        }
    }
}

void control_panel_init(void) {
    win_control_panel.title = "Control Panel";
    win_control_panel.x = 200;
    win_control_panel.y = 150;
    win_control_panel.w = 350;
    win_control_panel.h = 300;
    win_control_panel.visible = false;
    win_control_panel.focused = false;
    win_control_panel.z_index = 0;
    win_control_panel.paint = control_panel_paint;
    win_control_panel.handle_key = control_panel_handle_key;
    win_control_panel.handle_click = control_panel_handle_click;
    win_control_panel.handle_right_click = NULL;
    win_control_panel.buf_len = 0;
    win_control_panel.cursor_pos = 0;
    
    // Generate patterns
    generate_lumberjack_pattern();
    generate_blue_diamond_pattern();
}

void control_panel_reset(void) {
    win_control_panel.focused = false;
    current_view = VIEW_MAIN;
    focused_field = -1;
    input_cursor = 0;
}
