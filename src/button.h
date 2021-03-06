/*
   xkbd - xlib based onscreen keyboard.

   Copyright (C) 2001 Matthew Allum
   Changes (C) 2017+ Denis Kaganovich

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*/

#ifndef _BUTTON_H_
#define _BUTTON_H_

button* button_new(keyboard *k);

int _max3( int a, int b, int c );

GC _createGC(keyboard *kb, int rev);

KeySym button_ks(char *txt);

void button_set_pixmap(button *b, char *txt);
void button_set_txt_ks(button *b, char *txt);
void button_set_slide_ks(button *b, char *txt, int dir);
int _button_get_txt_size(keyboard *kb, char *txt);
int button_calc_vwidth(button *b);

int button_render(button *b, int mode);
void button_paint(button *b);

int button_get_abs_x(button *b);
int button_get_abs_y(button *b);

#endif
