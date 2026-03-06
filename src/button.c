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


#ifdef USE_XPM
void button_set_pixmap(button *b, char *filename)
{
  XpmAttributes attrib;

  attrib.valuemask = XpmCloseness;
  attrib.closeness = 40000;

  if (XpmReadFileToPixmap( b->kb->display, b->kb->win, filename,
		       &b->pixmap, &b->kb->GCval.clip_mask, &attrib)
      != XpmSuccess )
    {
          fprintf(stderr, "xkbd: failed loading image '%s'\n", filename);
	  exit(1);
    }

  b->mask_gc = _createGC(b->kb,GC0|GCClipMask);

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
	(FcChar8 *) txt, strlen(txt),
	&extents);
  return extents.width;
#elif defined(F_UTF8)
  XRectangle r1,r2;
  Xutf8TextExtents(strlen1utf8(txt)?kb->font1:kb->font,(char *) txt, strlen(txt),&r1,&r2);
  return r1.width;
#else
  return XTextWidth(kb->font, txt, strlen(txt));
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
	XGetGCValues(kb->display,gc,GCForeground,&kb->GCval);
	return kb->GCval.foreground;
}

int button_render(button *b, int mode)
{
  keyboard *kb = b->kb;
  Display *dpy = kb->display;
  Pixmap backing = kb->backing;
  gcs_t gc = b->gc;

  int l=KBLEVEL(b);
  char *txt;
  int p = kb->pad;
//  int p = cache_pix==3?0:kb->pad;
  int x = b->vx + p;
  int y = b->vy + p;
//  int w = b->act_width - (p<<1);
//  int h = b->act_height - (p<<1);
  int w = b->act_width - p;
  int h = b->act_height - p;
  int line = ((kb->GCval.line_width+1)>>1);
  int fix = (line<<1)-kb->GCval.line_width;
  p = _max(p - line,0);

  int ax = b->vx + p;
  int ay = b->vy + p;
  int aw = b->act_width;
  int ah = b->act_height;
  //aw-=p<<1; ah-=p<<1;
  aw+=fix; ah+=fix;

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
	XCopyArea(dpy, pix, backing, kb->gc.bg, 0, 0, aw , ah , ax, ay);
	return backing==kb->win;
    }
    b->pix[i] = pix = XCreatePixmap(dpy,
	kb->win,
//	backing,
	aw, ah,
	DefaultDepth(dpy, kb->screen) );
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
      gc.bg = gc.rev;
      if (gc.bdr_rev) gc.bdr = gc.bdr_rev;
      if (no_lock) {
	gc.txt   = gc.txt_rev;
#ifdef USE_XFT
	gc.col  = gc.col_rev;
#endif
     }
    }
  else if(mode & STATE(OBIT_LOCKED))
    {
      gc.txt   = gc.txt_rev;
      gc.bg = gc.rev;
      if (gc.bdr_rev) gc.bdr = gc.bdr_rev;
#ifdef USE_XFT
      gc.col  = gc.col_rev;
#endif
    }
//  else  /* BUTTON_RELEASED */
//    {
//      gc.rev = gc.bg;
//    }

  int x1=x, y1=y, x2=w, y2=h;
  switch (kb->theme) {
    case arc:
    case rounded:
    case square:
	x1+=line;
	y1+=line;
	x2-=line<<1;
	y2-=line<<1;
	x2+=fix;
	y2+=fix;
  }

  if (noFilled(gc.bg))
    switch (kb->theme) {
	case arc:
		XFillArc(dpy, backing, gc.bg, x1, y1, x2, y2, 0, 360 * 64);
		break;
	default:
		XFillRectangle(dpy, backing, gc.bg, x1, y1, x2, y2);
		break;
    }

 // GC g = (kb->GCval.line_width || b->gc.bg == kb->gc.bg)?kb->gc.bdr:b->gc.bg;
  GC g = (kb->GCval.line_width || gc.bg == kb->gc.bg)?gc.bdr:gc.bg;

  if (!(mode & STATE(OBIT_DIRECT)) || gc.bdr_rev)
  //if (noFilled(g)) {
   switch (kb->theme) {
    case arc:
	if (kb->GCval.line_width) XDrawArc(dpy, backing, g, x, y, w, h, 0, 360 * 64);
	else XFillArc(dpy, backing, g, x, y, w, h, 0, 360 * 64);
	break;
    case rounded:
	if (kb->GCval.line_width) {
		x2+=x1-1;
		y2+=y1-1;
		XDrawPoint(dpy, backing, g, x1, y1);
		XDrawPoint(dpy, backing, g, x2, y1);
		XDrawPoint(dpy, backing, g, x1, y2);
		XDrawPoint(dpy, backing, g, x2, y2);
//		XDrawPoint(dpy, backing, gc.txt, x1, y1); // debug
	}
    case square:
	if (kb->GCval.line_width) XDrawRectangle( dpy, backing, g, x, y, w, h);
	else XFillRectangle( dpy, backing, g, x, y, w, h);
	break;
  }

  if (b->pixmap)
    {
      /* TODO: improve alignment of images, kinda hacked at the mo ! */
      int xx = x+((w - b->vwidth)>>1);
      int yy = y+((h - b->vheight)>>1);

      kb->GCval.clip_x_origin = xx;
      kb->GCval.clip_y_origin = yy;
      XCopyGC(dpy,g,GCBackground|GCForeground,b->mask_gc);
      XChangeGC(dpy, b->mask_gc, GCClipXOrigin|GCClipYOrigin, &kb->GCval);

      XCopyArea(dpy, b->pixmap, backing, b->mask_gc,
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
    int len1 = strlen1utf8(txt);
    int xx = x+((w - _but_size(b,l))>>1);
    int yy = y+((h - (b->vheight?:len1?kb->finfo1.height:kb->finfo.height))>>1);
#ifdef USE_XFT
    XftDrawStringUtf8(kb->xftdraw, &gc.col, len1?kb->font1:kb->font,
		xx, yy + (len1?kb->finfo1.ascent:kb->finfo.ascent),
		(unsigned char *) txt, strlen(txt));
#elif defined(F_UTF8)
//setlocale(LC_CTYPE,"");
    Xutf8DrawString(dpy, backing, len1?kb->font1:kb->font, gc.txt,
		xx, yy + (len1?kb->finfo1.ascent:kb->finfo.ascent),
		txt, strlen(txt));
#else
    XDrawString(dpy, backing, gc.txt,
		xx, yy + (len1?kb->finfo1.ascent:kb->finfo.ascent),
		txt, strlen(txt));
#endif
  }
pixmap:
#ifdef CACHE_PIX
  if (cache_pix)
	XCopyArea(dpy, backing, pix, kb->gc.bg,
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
	int line = ((b->kb->GCval.line_width+1)>>1);
	p = _max(p - line,0);
	int x = b->vx + p;
	int y = b->vy + p;
	//p<<=1;
	p-=1;
	XCopyArea(b->kb->display, b->kb->backing, b->kb->win, b->kb->gc.bg,
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
  button *b = calloc1(button);
  b->kb = k;

  b->gc = k->gc;
//  b->gc.txt = b->gc.txt_rev = NULL;

  b->layout_switch = -1;

  return b;
}


