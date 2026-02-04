#ifndef EDITOR_H
#define EDITOR_H

#include "wm.h"

extern Window win_editor;

void editor_init(void);
void editor_open_file(const char *filename);

#endif
