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

#ifndef _KBD_H_
#define _KBD_H_
keyboard* kb_new(Window win, Display *display, int kb_x, int kb_y,
		 int kb_width, int kb_height, char *conf_file,
		 char *font_name, char *font_name1);
int kb_switch_layout(keyboard *kb, int kbd_layout_num, int shift);
void kb_send_keypress(button *b, unsigned int next_state, unsigned int press);
void kb_size(keyboard *kb);
void kb_repaint(keyboard *kb);
void kb_resize(keyboard *kb, int width, int height);
void kb_destroy(keyboard *kb);
button *kb_handle_events(keyboard *kb, int type, int x, int y, uint32_t ptr, int dev, Time time);
Bool kb_do_repeat(keyboard *kb, button *b);
void kb_set_slide(button *b, int x, int y);
void kb_process_keypress(button *b, int repeat, unsigned int press);
button * kb_find_button(keyboard *kb, int x, int y);
int _XColorFromStr(Display *display, XColor *col, const char *defstr);
unsigned int kb_sync_state(keyboard *kb, unsigned int mods, unsigned int locked_mods, int group);

int kb_load_keymap();

#endif




