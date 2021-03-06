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

#define DEFAULT_FONT "Monospace-%i|-%i|sans-%i|fixed-%i|fixed"

typedef uint_fast8_t _ushort;
typedef int_fast16_t _sshort;

// to make 1-pointer multibutton devices simple - define
// for button acrobats - undef
#define BUTTONS_TO1 1

// if you sure in your touch/drivers - can change
#define z_t double
//#define z_t int

// shift,mod/alt
//#define LEVEL_BITS 2
// +ctrl-alt
#define LEVEL_BITS 3

#define STD_LEVELS (1U<<LEVEL_BITS)

#define OBIT_OBEYCAPS	0
#define OBIT_WIDTH_SPEC	1
#define OBIT_PRESSED	2
#define OBIT_LOCKED	3
#define OBIT_UGLY	4
#define OBIT_LOCK	5
#define OBIT_DIRECT	6

#define KBIT_SHIFT	0
#define KBIT_CAPS	1
#define KBIT_CTRL	2
#define KBIT_ALT	3
// numlock
#define KBIT_MOD	4
#define KBIT_META	5

#define STATE(b)	(1U<<b)
#define BIT_MV(m,b,b2)	(((m) & STATE(b))>>(b-b2))
#define BIT_MVL(m,b,b2)	(((m) & STATE(b))<<(b2-b))
static inline unsigned int LEVEL(unsigned int m, unsigned int o){
//	return ((BIT_MV(m,KBIT_SHIFT,0)^(BIT_MV(m,KBIT_CAPS,0)&BIT_MV(o,OBIT_OBEYCAPS,0) ))|(BIT_MV(m,KBIT_MOD,1)^BIT_MV(m,KBIT_ALT,1)))
	return ((BIT_MV(m,KBIT_SHIFT,0)^(BIT_MV(m,KBIT_CAPS,0)&BIT_MV(o,OBIT_OBEYCAPS,0) ))|BIT_MV(m,KBIT_MOD,1))
#if LEVEL_BITS == 3
		|(BIT_MV(m,KBIT_CTRL,2)&BIT_MV(m,KBIT_ALT,2))
#endif
	;
}
static inline unsigned int MODS(unsigned int l){
//	const unsigned int alt=BIT_MVL(l,1,KBIT_ALT);
	const unsigned int alt=BIT_MVL(l,1,KBIT_MOD);
#if LEVEL_BITS == 3
	return BIT_MVL(l,0,KBIT_SHIFT)|((l&4)?STATE(KBIT_CTRL)|STATE(KBIT_ALT):alt);
#else
	return BIT_MVL(l,0,KBIT_SHIFT)|alt;
#endif
}

#define KBLEVEL(b)	LEVEL(b->kb->state|b->kb->state_locked,b->flags)
#define KBDLEVEL(kb)	LEVEL(kb->state|kb->state_locked,0)

#define KB_STATE_KNOWN  (STATE(KBIT_SHIFT)|STATE(KBIT_CAPS)|STATE(KBIT_CTRL)|STATE(KBIT_ALT)|STATE(KBIT_MOD))

#define TRUE            1
#define FALSE           0

#define UP              1
#define DOWN            2
#define LEFT            3
#define RIGHT           4

#define MAX_LAYOUTS     3

// features

// cache pixmaps increase render speed 5x and RAM 0.3-2.5k per layout
#define CACHE_SIZES
#define CACHE_PIX

#ifndef MINIMAL
//#define SLIDES
#define SIBLINGS
#define GESTURES_EMULATE
#define GESTURES_USE
#endif

#ifdef USE_XI
#define MULTITOUCH
// can count touches and reuse overcounted over device capabilities
// but IMHO no lost touches observed. so, keep code disabled
//#define COUNT_TOUCHES
#else
#undef MULTITOUCH
#endif

#ifdef MULTITOUCH
#define TOUCH_SHIFT 5
#else
#define TOUCH_SHIFT 0
#undef SIBLINGS
#endif

#define MAX_SIBLINGS 16


#define STD_LEVELS (1U<<LEVEL_BITS)

#ifdef SLIDES
#define LEVELS 8
#else
#define LEVELS STD_LEVELS
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
  int min_width;        /* ( num_kids*(kid_vwidth+(kid_border*2) ) */
  int min_height;
  int act_width;        /* actual calculated width */
  int act_height;       /* ( num_kids*(kid_vwidth+padding+(kid_border*2) ) */
  int x;                /* relative to parent ? */
  int y;
  int width;        // relative/mm
  int height;
  int undef;
  int vx;
  int vy;

  struct _box *parent;  /* pointer to parent keyboard */

} box;

#ifdef USE_XFT
#define FNTYPE XftFont *
#else
#define FNTYPE XFontStruct *
#endif

typedef struct _keyboard
{
  int mode;
  box *kbd_layouts[MAX_LAYOUTS];
  int total_layouts;
  int group;
  box *vbox;  /* container */
  box *vvbox;  /* visible */

  long width, height; // mm|relative

  int act_width, act_height; // ConfigureNotify - touch control only

  int vheight;
  int vheight1;

  int line_width;
  int pad;

  Window win;
  Display *display;
  int screen;
  Colormap colormap;
  Visual *visual;
#ifdef CACHE_PIX
  Pixmap backing;
#else
#define backing win
#endif

  GC gc;
  GC rev_gc;   /* inverse gc of above */
  GC txt_gc;   /* gc's for button txt */
  GC txt_rev_gc;
  GC bdr_gc;

  GC grey_gc;
  GC kp_gc;

  GC filled;

  FNTYPE font;
  FNTYPE font1;

  unsigned int state;  /* shifted | caps | modded | normal */
  unsigned int state_locked;  /* shifted | modded | normal */

  enum { rounded, square, plain } theme;

  int slide_margin;
  int key_delay_repeat; /* delay time before key repeat */
  int key_repeat;       /* delay time between key repeats */
  unsigned int key_delay_repeat1;
  unsigned int key_repeat1;

#ifdef USE_XFT
  XftDraw *xftdraw;   /* xft aa bits */
  XftColor color;
  XftColor color_rev;
#endif

  int def_width;
  int def_height;
  int kp_width;

  int X,Y; // root

} keyboard;

typedef struct _button
{
  int x;             /* actual co-ords relative to window */
  int y;
  int width, height; // relative or mm
  int vx,vy;

  KeyCode kc[LEVELS];
  unsigned int mods[LEVELS];
#ifdef SIBLINGS
  void **siblings;
  unsigned short nsiblings;
#endif
  int z; // pressure

  char *txt[STD_LEVELS];
  KeySym ks[LEVELS];
#ifdef CACHE_SIZES
  int txt_size[STD_LEVELS];
#endif
#ifdef CACHE_PIX
  Pixmap pix[STD_LEVELS<<2];
#endif

#define GET_TXT(b,i)	(b->txt[i])
#define GET_TXT_SIZE(b,i)	(b->txt_size[i])
#define GET_KS(b,i)	(b->ks[i])
#define SET_KS(b,i,k)	{ b->ks[i] = k; }

#define DEFAULT_TXT(b) GET_TXT(b,0)
#define DEFAULT_TXT_SIZE(b) GET_TXT_SIZE(b,0)
#define SHIFT_TXT(b) GET_TXT(b,1)
#define MOD_TXT(b) GET_TXT(b,2)
#define SHIFT_MOD_TXT(b) GET_TXT(b,3)

#define DEFAULT_KS(b) GET_KS(b,0)
#define SHIFT_KS(b) GET_KS(b,1)
#define MOD_KS(b) GET_KS(b,2)
#define SHIFT_MOD_KS(b) GET_KS(b,3)

#define SLIDE_DOWN	4
#define SLIDE_UP	5
#define SLIDE_LEFT	6
#define SLIDE_RIGHT	7

#ifdef SLIDES
  short slide;
#endif

  unsigned int modifier; /* set to BUT_ if key is shift,ctrl,caps etc */

  unsigned int flags; /* bit-field of OPT_* */

  int vwidth;  /* width  of contents ( min width ) */
  int vheight; /* height of contents ( min height ) */
  int b_size;   /* size of border in pixels */
                /* eg. total width = vwidth+pad_x+(2*b_size) */

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
  XftColor col;
  XftColor col_rev;
#endif

  Pixmap pixmap;
  Pixmap mask;
  GC mask_gc;

  int cnt;
}
//    __attribute__ ((__packed__))
    button;


extern int Xkb_sync;
extern int no_lock;
extern int swipe_fingers;
// todo: width/height in mm
extern unsigned long int scr_width;
extern unsigned long int scr_height;
extern unsigned long scr_mwidth;
extern unsigned long scr_mheight;
#ifdef COUNT_TOUCHES
extern uint8_t num_touches;
#endif
#ifdef CACHE_PIX
extern int cache_pix;
#else
#define cache_pix 0
#endif

static inline long _min(long x,long y){ return x<y?x:y; }
static inline long _max(long x,long y){ return x>y?x:y; }

static inline int strlen1utf8(char *s) {
	int cnt=0;
	while (*s && cnt<2) cnt += (*s++ & 0xC0) != 0x80;
	return cnt==1;
}

#endif


