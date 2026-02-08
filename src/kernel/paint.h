#ifndef PAINT_H
#define PAINT_H

#include "wm.h"

extern Window win_paint;

void paint_init(void);
void paint_reset(void);
void paint_handle_mouse(int x, int y);
void paint_load(const char *path);
void paint_reset_last_pos(void);

#endif