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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_XPM
#include <X11/xpm.h>
#endif
#include <X11/keysymdef.h>
#include "structs.h"
#include "button.h"


GC _createGC(keyboard *kb, int rev)
{
	Display *dpy = kb->display;
	unsigned long b = BlackPixel(dpy, kb->screen);
	unsigned long w = WhitePixel(dpy, kb->screen);

	XGCValues values = {
		.foreground=rev?w:b,
		.background=rev?b:w,
		.fill_style=FillSolid,
		.graphics_exposures=0,
	};

	return XCreateGC(dpy, kb->win, GCGraphicsExposures|GCBackground|GCForeground|GCFillStyle, &values);
}

#ifdef USE_XPM
void button_set_pixmap(button *b, char *filename)
{
  XpmAttributes attrib;
  //XGCValues gc_vals;
  //unsigned long valuemask = 0;

  attrib.valuemask = XpmCloseness;
  attrib.closeness = 40000;

  if (XpmReadFileToPixmap( b->kb->display, b->kb->win, filename,
		       &b->pixmap, &b->mask, &attrib)
      != XpmSuccess )
    {
          fprintf(stderr, "xkbd: failed loading image '%s'\n", filename);
	  exit(1);
    }

  /* we'll also be needing a gc for transparency */
  b->mask_gc = _createGC(b->kb,1);
  /*
  gc_vals.clip_mask = b->mask;
  valuemask = GCClipMask;
  XChangeGC(b->kb->display, b->mask_gc, valuemask, &gc_vals);
  */
  XSetClipMask(b->kb->display, b->mask_gc, b->mask);

  b->vwidth  = attrib.width;
  b->vheight = attrib.height;
}
#endif

void button_set_txt_ks(button *b, char *txt)
{
  if (!strcmp(txt, "Caps_Lock")) {
    b->modifier = STATE(KBIT_CAPS);
    b->flags |= STATE(OBIT_LOCK);
  } else if (!strncmp(txt, "Shift", 5))
    b->modifier = STATE(KBIT_SHIFT);
  else if (!strncmp(txt, "Control", 7) || !strncmp(txt, "Ctrl", 4))
    b->modifier = STATE(KBIT_CTRL);
  else if (!strncmp(txt, "Alt",3))
      b->modifier = STATE(KBIT_ALT);
  else if (!strncmp(txt, "Meta",4))
      b->modifier = STATE(KBIT_META);
  else if (!strcmp(txt, "Num_Lock")) {
	b->modifier = STATE(KBIT_MOD);
	b->flags |= STATE(OBIT_LOCK);
  } else if (!strncmp(txt, "!Mod", 3)) {
	b->modifier = STATE(KBIT_MOD);
//	SET_KS(b,0,0);
	txt=NULL;
  }

  if (txt) {
	KeySym ks = XStringToKeysym(txt);
	SET_KS(b,0,ks);
	if (!ks) fprintf(stderr, "Cant find keysym for %s \n", txt);
//	else if(!b->modifier) {
//		int m = XkbKeysymToModifiers(b->kb->display, ks);
//		if(m) fprintf(stderr,"Key=%s KeySym=0x%x modifiers=0x%x\n",txt,ks,m);
//	}


	/* for backwards compatibility */
	if (DEFAULT_KS(b) >= 0x061 && DEFAULT_KS(b) <= 0x07a)
	b->flags |= STATE(OBIT_OBEYCAPS);
  }

}

void button_set_slide_ks(button *b, char *txt, int dir)
{
  KeySym tmp_ks;
  if ( (tmp_ks = XStringToKeysym(txt)) == 0) /* NoSymbol ?? */
    {
      fprintf(stderr, "Cant find keysym for %s \n", txt);
      return;
    }

  switch(dir)
    {
      case UP    : SET_KS(b,SLIDE_UP,tmp_ks); break;
      case DOWN  : SET_KS(b,SLIDE_DOWN,tmp_ks); break;
      case LEFT  : SET_KS(b,SLIDE_LEFT,tmp_ks); break;
      case RIGHT : SET_KS(b,SLIDE_RIGHT,tmp_ks); break;
    }
}

KeySym button_ks(char *txt)
{
  KeySym res;
  if ((res = XStringToKeysym(txt)) == (KeySym)NULL)
    fprintf(stderr, "Cant find keysym for %s \n", txt);
  return res;
}

int _button_get_txt_size(keyboard *kb, char *txt)
{
  if (!txt) return 0;
#ifdef USE_XFT
  XGlyphInfo       extents;
  XftTextExtentsUtf8(kb->display, strlen1utf8(txt)?kb->font1:kb->font,
	(unsigned char *) txt, strlen(txt),
	&extents);
  return extents.width;
#else
  return XTextWidth(kb->font_info, txt, strlen(txt));
#endif
}

static int _but_size(button *b, int l){
#ifdef CACHE_SIZES
	int i,s=0;
	if (!(s=b->txt_size[l])) {
		// set size=1 to empty as checked
		s=_button_get_txt_size(b->kb, b->txt[l])?:1;
		for (i=0; i<STD_LEVELS; i++) if (b->txt[i]==b->txt[l]) b->txt_size[i]=s;
	}
	return s;
#else
	return _button_get_txt_size(b->kb, b->txt[l]);
#endif
}

int button_calc_vwidth(button *b)
{
  if (b->vwidth ) return b->vwidth; /* already calculated from image or width_param */

#ifdef CACHE_SIZES
  int i,sz = 0;
  for (i=0; i<STD_LEVELS; i++) if (b->txt[i]) sz = _max(sz,_but_size(b,i));
  b->vwidth = sz;
#else
  b->vwidth = _max(
	_button_get_txt_size(b->kb, DEFAULT_TXT(b)),
    _max(_button_get_txt_size(b->kb, SHIFT_TXT(b)),
	_button_get_txt_size(b->kb, MOD_TXT(b))
    ));
#endif
  if (b->vwidth < 2
//	&& !(DEFAULT_KS(b) || SHIFT_KS(b) || MOD_KS(b)) &&
//	DEFAULT_TXT(b) == NULL && SHIFT_TXT(b) == NULL && MOD_TXT(b) == NULL
  ) b->flags|=STATE(OBIT_DIRECT); // reuse bit as blank/spacer

  return b->vwidth;
}

static int button_set_b_size(button *b, int size)
{
   b->b_size = size;
   return size;
}

static unsigned long getGCFill(keyboard *kb, GC gc){
	XGCValues v;
	XGetGCValues(kb->display,gc,GCForeground,&v);
	return v.foreground;
}

int button_render(button *b, int mode)
{
  /*
    set up a default gc to point to whatevers gc is not NULL
     moving up via button -> box -> keyboard
  */
  GC gc_txt;
  GC gc_solid;

#ifdef USE_XFT
  XftColor tmp_col;
#endif

  int l=KBLEVEL(b);
  char *txt;
  keyboard *kb = b->kb;
  Pixmap backing = kb->backing;
  int p = kb->pad;
//  int p = cache_pix==3?0:kb->pad;
  int x = b->vx + p;
  int y = b->vy + p;
  int w = b->act_width - (p<<1);
  int h = b->act_height - (p<<1);
  int line = ((kb->line_width+1)>>1);
  p = _max(p - line,0);
  int ax = b->vx + p;
  int ay = b->vy + p;
  p <<= 1;
  int aw = b->act_width - p;
  int ah = b->act_height - p;

#ifdef CACHE_PIX
  Pixmap pix = None;
  int i, m, j;

   // reuse bit as blank/spacer
  if (b->flags & STATE(OBIT_DIRECT)) return 1;
  if (cache_pix) {

#if OBIT_PRESSED == 2 && OBIT_LOCKED == 3
    m = ((mode>>2)&3);
#else
    m = BIT_MV(mode,OBIT_PRESSED,0)|BIT_MV(mode,OBIT_LOCKED,1);
#endif
    i = (l<<2)|m;
    pix = b->pix[i];
    if (pix) {
	if (mode & STATE(OBIT_DIRECT)) {
		backing = cache_pix==4?backing:kb->win;
		ax+=kb->vvbox->x;
		ay+=kb->vvbox->y;
	}
	XCopyArea(kb->display, pix, backing, kb->gc, 0, 0, aw, ah, ax, ay);
	return backing==kb->win;
    }
    b->pix[i] = pix = XCreatePixmap(kb->display,
	kb->win,
//	backing,
	aw, ah,
	DefaultDepth(kb->display, kb->screen) );
//   if (cache_pix==3) backing=pix;
  }
    // pixmap must pass too as txt[]=0
  txt = GET_TXT(b,l);
  if (!txt) {
	l=0;
	txt = DEFAULT_TXT(b);
  }
  if (cache_pix) {
    for (j=m&1; j>=0; j--) {
	for (i=0; i<STD_LEVELS; i++) if (b->txt[i]==txt || (!l && !b->txt[i])) b->pix[(i<<2)|m]=pix;
	m^=2;
    }
  }
#endif

//  b->flags = (b->flags & ~(STATE(OBIT_PRESSED)|STATE(OBIT_LOCKED)|BUTTON_RELEASED))|mode;
  if (mode & STATE(OBIT_PRESSED))
    {
	
      gc_solid = b->fg_gc;
      if (no_lock) {
	gc_txt   = kb->txt_rev_gc;
#ifdef USE_XFT
	tmp_col  = b->col_rev;
#endif
     } else {
	gc_txt   = b->txt_gc;
#ifdef USE_XFT
	tmp_col = b->col;
#endif
     }
    }
  else if(mode & STATE(OBIT_LOCKED))
    {
      gc_solid = b->fg_gc;
      gc_txt   = kb->txt_rev_gc;
#ifdef USE_XFT
      tmp_col  = b->col_rev;
#endif
    }
  else  /* BUTTON_RELEASED */
    {
      gc_solid = b->bg_gc;
      gc_txt   = b->txt_gc;
#ifdef USE_XFT
      tmp_col = b->col;
#endif
    }

  int x1=x, y1=y, x2=w, y2=h;
  switch (kb->theme) {
    case rounded:
    case square:
	x1+=line;
	y1+=line;
	x2-=line<<1;
	y2-=line<<1;
  }

  if (!kb->filled || (gc_solid!=kb->filled && getGCFill(kb,kb->filled)!=getGCFill(kb,gc_solid)))
	XFillRectangle(kb->display, backing, gc_solid, x1, y1, x2, y2);

  switch (kb->theme) {
    case rounded:
	XDrawRectangle( kb->display, backing, kb->bdr_gc, x, y, w, h);
	x2+=x;
	y2+=y;
	XDrawPoint(kb->display, backing, kb->bdr_gc, x1, y1);
	XDrawPoint(kb->display, backing, kb->bdr_gc, x2, y1);
	XDrawPoint(kb->display, backing, kb->bdr_gc, x1, y2);
	XDrawPoint(kb->display, backing, kb->bdr_gc, x2, y2);
	break;
    case square:
	if (!(mode & STATE(OBIT_DIRECT)))
	XDrawRectangle( kb->display, backing, kb->bdr_gc, x, y, w, h);
	break;
  }


  if (b->pixmap)
    {
      /* TODO: improve alignment of images, kinda hacked at the mo ! */
      XGCValues gc_vals;
      int xx = x+((w - b->vwidth)>>1);
      int yy = y+((h - b->vheight)>>1);

      gc_vals.clip_x_origin = xx;
      gc_vals.clip_y_origin = yy;
      XChangeGC(kb->display, b->mask_gc, GCClipXOrigin|GCClipYOrigin, &gc_vals);

      XCopyArea(kb->display, b->pixmap, backing, b->mask_gc,
		0, 0, b->vwidth, b->vheight, xx, yy);
      goto pixmap; /* imgs cannot have text aswell ! */
    }

#ifndef CACHE_PIX
//  txt = GET_TXT(b,KBLEVEL(b))?:DEFAULT_TXT(b);
  txt = GET_TXT(b,l);
  if (!txt) {
	l=0;
	txt = DEFAULT_TXT(b);
  }
#endif
 
  if (txt) {
    int xx = x+((w - _but_size(b,l))>>1);
    int yy = y+((h - (b->vheight?:strlen1utf8(txt)?kb->vheight1:kb->vheight))>>1);
#ifdef USE_XFT
    XftDrawStringUtf8(kb->xftdraw, &tmp_col, strlen1utf8(txt)?kb->font1:kb->font,
		xx, yy + kb->font->ascent,
		(unsigned char *) txt, strlen(txt));
#else
    XDrawString(kb->display, backing, gc_txt,
		xx, yy + kb->font->ascent,
		txt, strlen(txt));
#endif
  }
pixmap:
#ifdef CACHE_PIX
  if (cache_pix)
	XCopyArea(kb->display, backing, pix, kb->gc,
		ax, ay, aw, ah, 0, 0);
#endif
   return backing == kb->win;
}

void button_paint(button *b)
{
#ifdef CACHE_PIX
    if (b->kb->backing != b->kb->win) {
  /* use the vbox offsets for the location within the window */
	int p = b->kb->pad;
	int x = b->vx + p;
	int y = b->vy + p;

	p<<=1;
	XCopyArea(b->kb->display, b->kb->backing, b->kb->win, b->kb->gc,
	    x, y, b->act_width - p, b->act_height - p,
	    x+b->kb->vvbox->x, y+b->kb->vvbox->y);
    }
#endif
}

int button_get_abs_x(button *b)
{
  int total = b->x;
  box *tmp_box = b->parent;
  while (tmp_box != NULL)
    {
      total += tmp_box->x;
      tmp_box = tmp_box->parent;
    }
  /* total = total - b->kb->vvbox->x;  HACK ! */

  return total;
}

int button_get_abs_y(button *b)
{
  int total = b->y;
  box *tmp_box = b->parent;
  while (tmp_box != NULL)
    {
      total += tmp_box->y;
      tmp_box = tmp_box->parent;
    }

  return total;
}

button* button_new(keyboard *k)
{
  button *b = stalloc(sizeof(button));
  b->kb = k;

  b->fg_gc      = k->gc;
  b->bg_gc      = k->rev_gc;
  b->txt_gc     = k->txt_gc;
  b->col = k->color;
  b->col_rev = k->color_rev;

  b->layout_switch = -1;

  return b;
}


