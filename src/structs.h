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

#ifndef _STRUCT_H_
#define _STRUCT_H_

#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#ifdef USE_XFT
#include <X11/Xft/Xft.h>
#endif
#define BUTTON_PRESSED  0
#define BUTTON_RELEASED 1
#define BUTTON_LOCKED   2

#define OBIT_OBEYCAPS	0
#define OBIT_WIDTH_SPEC	1

#define KBIT_SHIFT	0
#define KBIT_CAPS	1
#define KBIT_CTRL	2
#define KBIT_ALT	3
#define KBIT_META	4
#define KBIT_MOD	5

#define STATE(b)	(1U<<b)
#define BIT_MV(m,b,b2)	(((m) & STATE(b))>>(b-b2))
inline unsigned int LEVEL(unsigned int m, unsigned int l, unsigned int o){
	return ((BIT_MV(m,KBIT_SHIFT,0)^(BIT_MV(m,KBIT_CAPS,0)&BIT_MV(o,OBIT_OBEYCAPS,0) ))|(BIT_MV(m,KBIT_MOD,1)^BIT_MV(m,KBIT_ALT,1)));
}
#define KBLEVEL(b)	LEVEL(b->kb->state,b->kb->state_locked,b->options)

#define KB_STATE_KNOWN  (STATE(KBIT_SHIFT)|STATE(KBIT_CAPS)|STATE(KBIT_CTRL)|STATE(KBIT_ALT))


#define TRUE            1
#define FALSE           0

#define UP              1
#define DOWN            2
#define LEFT            3
#define RIGHT           4

#define MAX_LAYOUTS     3

// features
#define SEQ_CACHE

#ifndef MINIMAL
#define SLIDES
#define SIBLINGS
#endif

#ifdef USE_XI
#define MULTITOUCH
#else
#undef MULTITOUCH
#endif

#ifdef MULTITOUCH
#define MAX_TOUCH 10
#else
#define MAX_TOUCH 1
#undef SIBLINGS
#endif


#define MAX_SIBLINGS 127

#ifdef SLIDES
#define LEVELS 8
#else
#define LEVELS 4
#endif


typedef struct _list
{
  struct _list *next;
  int type;
  void *data;

} list;


typedef struct _box
{
  enum { vbox, hbox } type;
  list *root_kid;
  list *tail_kid;
  int min_width;        /* ( num_kids*(kid_c_width+(kid_border*2) ) */
  int min_height;
  int act_width;        /* actual calculated width */
  int act_height;       /* ( num_kids*(kid_c_width+padding+(kid_border*2) ) */
  int x;                /* relative to parent ? */
  int y;

  struct _box *parent;  /* pointer to parent keyboard */

} box;

typedef struct _keyboard
{
  int mode;
  box *kbd_layouts[MAX_LAYOUTS];
  int total_layouts;
  int group;
  box *vbox;  /* container */

  int x;      /* but vbox contains this ? */
  int y;
  Window win;
  Display *display;
  Pixmap backing;

  GC gc;
  GC rev_gc;   /* inverse gc of above */
  GC txt_gc;   /* gc's for button txt */
  GC txt_rev_gc;
  GC bdr_gc;

  XFontStruct* font_info;
  unsigned int state;  /* shifted | caps | modded | normal */
  unsigned int state_locked;  /* shifted | modded | normal */

  enum { oldskool, xft } render_type;
  enum { rounded, square, plain } theme;

  int slide_margin;
  int key_delay_repeat; /* delay time before key repeat */
  int key_repeat;       /* delay time between key repeats */
  unsigned int key_delay_repeat1;
  unsigned int key_repeat1;

#ifdef USE_XFT
  XftDraw *xftdraw;   /* xft aa bits */
  XftFont *xftfont;
  XftColor color_bg;
  XftColor color_fg;
#endif

} keyboard;

typedef struct _button
{
  int x;             /* actual co-ords relative to window */
  int y;

#ifdef SEQ_CACHE
  void *cache[LEVELS];
#endif
#ifdef SIBLINGS
  void **siblings;
  unsigned short nsiblings;
#endif

  char *txt[4];
  KeySym ks[LEVELS];

#define GET_TXT(b,i)	(b->txt[i])
#define GET_KS(b,i)	(b->ks[i])
#define SET_KS(b,i,k)	{ b->ks[i] = k; }

#define DEFAULT_TXT(b) GET_TXT(b,0)
#define SHIFT_TXT(b) GET_TXT(b,1)
#define MOD_TXT(b) GET_TXT(b,2)
#define SHIFT_MOD_TXT(b) GET_TXT(b,3)

#define DEFAULT_KS(b) GET_KS(b,0)
#define SHIFT_KS(b) GET_KS(b,1)
#define MOD_KS(b) GET_KS(b,2)
#define SHIFT_MOD_KS(b) GET_KS(b,3)

#define SLIDE_UP	4
#define SLIDE_DOWN	5
#define SLIDE_LEFT	6
#define SLIDE_RIGHT	7

#ifdef SLIDES
  short slide;
#endif

  unsigned int modifier; /* set to BUT_ if key is shift,ctrl,caps etc */

  unsigned int options; /* bit-field of OPT_* */

  int c_width;  /* width  of contents ( min width ) */
  int c_height; /* height of contents ( min height ) */
  int x_pad;    /* total padding horiz */
  int y_pad;    /* total padding vert  */
  int b_size;   /* size of border in pixels */
                /* eg. total width = c_width+pad_x+(2*b_size) */

   int key_span_width; /* width in number of keys spanned */

  int act_width;
  int act_height;

  keyboard *kb;   /* pointer to parent keyboard */
  box   *parent;  /* pointer to holding box */

  GC fg_gc;       /* gc's for 'general' button cols */
  GC bg_gc;

  signed int layout_switch; /* Signals the button switches layout
			       set to -1 for no switch            */

#ifdef USE_XFT
  XftColor *xft_fg_col;  /* xft */
  XftColor *xft_bg_col;
#endif

  Pixmap *pixmap;
  Pixmap *mask;
  GC mask_gc;

}
//    __attribute__ ((__packed__)) 
    button;


#ifdef MINIMAL
#define Xkb_sync 0
#else
extern int Xkb_sync;
#endif

extern int no_lock;

#endif


