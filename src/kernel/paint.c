#include "paint.h"
#include "graphics.h"
#include "wm.h"
#include "memory_manager.h"
#include "fat32.h"

#define CANVAS_W 300
#define CANVAS_H 200
#define PAINT_MAGIC 0x544E5042 // 'BPNT'

Window win_paint;
static uint32_t *canvas_buffer = NULL;
static uint32_t current_color = COLOR_BLACK;
static int last_mx = -1;
static int last_my = -1;

static void paint_paint(Window *win) {
    // Background
    draw_rect(win->x + 4, win->y + 24, win->w - 8, win->h - 28, COLOR_LTGRAY);
    
    // Toolbar area
    draw_rect(win->x + 10, win->y + 30, 40, win->h - 40, COLOR_GRAY);
    draw_bevel_rect(win->x + 10, win->y + 30, 40, win->h - 40, true);
    
    // Color Palette
    uint32_t colors[] = {COLOR_BLACK, COLOR_RED, COLOR_APPLE_GREEN, COLOR_APPLE_BLUE, COLOR_APPLE_YELLOW, COLOR_WHITE};
    for (int i = 0; i < 6; i++) {
        int cy = win->y + 40 + (i * 25);
        draw_rect(win->x + 15, cy, 30, 20, colors[i]);
        draw_rect(win->x + 15, cy, 30, 1, COLOR_BLACK);
        draw_rect(win->x + 15, cy, 1, 20, COLOR_BLACK);
        draw_rect(win->x + 44, cy, 1, 20, COLOR_BLACK);
        draw_rect(win->x + 15, cy + 19, 30, 1, COLOR_BLACK);
        
        // Highlight selected color
        if (current_color == colors[i]) {
            draw_rect(win->x + 13, cy - 2, 34, 2, COLOR_WHITE);
            draw_rect(win->x + 13, cy + 20, 34, 2, COLOR_WHITE);
        }
    }

    // Toolbar Buttons
    draw_button(win->x + 12, win->y + win->h - 65, 36, 20, "CLR", false);
    draw_button(win->x + 12, win->y + win->h - 40, 36, 20, "SAV", false);

    // Canvas Area
    int canvas_x = win->x + 60;
    int canvas_y = win->y + 30;
    draw_bevel_rect(canvas_x - 2, canvas_y - 2, CANVAS_W + 4, CANVAS_H + 4, true);
    
    if (canvas_buffer) {
        for (int y = 0; y < CANVAS_H; y++) {
            for (int x = 0; x < CANVAS_W; x++) {
                uint32_t color = canvas_buffer[y * CANVAS_W + x];
                put_pixel(canvas_x + x, canvas_y + y, color);
            }
        }
    }
}

static void paint_put_brush(int cx, int cy) {
    if (!canvas_buffer) return;
    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            if (px >= 0 && px < CANVAS_W && py >= 0 && py < CANVAS_H) {
                canvas_buffer[py * CANVAS_W + px] = current_color;
            }
        }
    }
    wm_mark_dirty(win_paint.x + 60 + cx, win_paint.y + 30 + cy, 2, 2);
}

void paint_handle_mouse(int x, int y) {
    int cx = x - 60;
    int cy = y - 30;

    if (cx < 0 || cx >= CANVAS_W || cy < 0 || cy >= CANVAS_H) {
        last_mx = -1;
        return;
    }

    if (last_mx == -1) {
        paint_put_brush(cx, cy);
    } else {
        // Bresenham's line algorithm to fill gaps
        int x0 = last_mx, y0 = last_my;
        int x1 = cx, y1 = cy;
        int dx = (x1 - x0 > 0) ? (x1 - x0) : (x0 - x1);
        int dy = (y1 - y0 > 0) ? (y1 - y0) : (y0 - y1);
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;

        while (1) {
            paint_put_brush(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
    }
    last_mx = cx;
    last_my = cy;
}

void paint_reset_last_pos(void) {
    last_mx = -1;
    last_my = -1;
}

static void paint_save(const char *path) {
    FAT32_FileHandle *fh = fat32_open(path, "w");
    if (fh) {
        uint32_t header[3] = {PAINT_MAGIC, CANVAS_W, CANVAS_H};
        fat32_write(fh, header, sizeof(header));
        fat32_write(fh, canvas_buffer, CANVAS_W * CANVAS_H * sizeof(uint32_t));
        fat32_close(fh);
        wm_show_message("Paint", "Image saved to Desktop.");
    }
}

void paint_load(const char *path) {
    FAT32_FileHandle *fh = fat32_open(path, "r");
    if (fh) {
        uint32_t header[3];
        if (fat32_read(fh, header, sizeof(header)) == sizeof(header)) {
            if (header[0] == PAINT_MAGIC) {
                fat32_read(fh, canvas_buffer, CANVAS_W * CANVAS_H * sizeof(uint32_t));
                win_paint.visible = true;
                win_paint.focused = true;
            }
        }
        fat32_close(fh);
    }
}

static void paint_click(Window *win, int x, int y) {
    (void)win;
    // Check Buttons
    if (x >= 12 && x < 48) {
        if (y >= win->h - 65 && y < win->h - 45) {
            paint_reset();
            return;
        }
        if (y >= win->h - 40 && y < win->h - 20) {
            paint_save("/Desktop/drawing.pnt");
            return;
        }
    }

    // Check Palette
    if (x >= 15 && x < 45) {
        for (int i = 0; i < 6; i++) {
            int cy = 40 + (i * 25);
            if (y >= cy && y < cy + 20) {
                uint32_t colors[] = {COLOR_BLACK, COLOR_RED, COLOR_APPLE_GREEN, COLOR_APPLE_BLUE, COLOR_APPLE_YELLOW, COLOR_WHITE};
                current_color = colors[i];
                return;
            }
        }
    }
    paint_handle_mouse(x, y);
}

void paint_init(void) {
    win_paint.title = "Paint";
    win_paint.x = 150;
    win_paint.y = 100;
    win_paint.w = 380;
    win_paint.h = 260;
    win_paint.visible = false;
    win_paint.focused = false;
    win_paint.z_index = 0;
    win_paint.paint = paint_paint;
    win_paint.handle_click = paint_click;
    win_paint.handle_right_click = NULL;
    win_paint.handle_key = NULL;

    if (!canvas_buffer) {
        canvas_buffer = (uint32_t*)kmalloc(CANVAS_W * CANVAS_H * sizeof(uint32_t));
        paint_reset();
    }
}

void paint_reset(void) {
    if (canvas_buffer) {
        for (int i = 0; i < CANVAS_W * CANVAS_H; i++) {
            canvas_buffer[i] = COLOR_WHITE;
        }
    }
}