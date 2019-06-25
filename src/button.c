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



int max3( int a, int b, int c )
{
  int rval;
  rval = a;
  if( b>rval ) rval=b;
  if( c>rval ) rval=c;
  return( rval );
}

GC _createGC(Display *display, Window win)
{
  GC gc;
  unsigned long valuemask = 0;
  XGCValues values;
  unsigned int line_width = 1;
  int line_style = LineSolid;
  int cap_style = CapRound;
  int join_style = JoinRound;


  gc = XCreateGC(display, win, valuemask, &values);
  XSetForeground(display, gc,
		 BlackPixel(display, DefaultScreen(display) ));
  XSetBackground(display, gc,
		 WhitePixel(display, DefaultScreen(display) ));

  XSetLineAttributes(display, gc, line_width, line_style,
		     cap_style, join_style );

  XSetFillStyle(display, gc, FillSolid);

  return gc;
}


int _XColorFromStr(Display *display, XColor *col, const char *defstr)
{
  char *str;
  const char delim[] = ",:";
  char *token;
  XColor exact;
  str = strdup(defstr);

  if ((strchr(defstr, delim[0]) != NULL)
      || (strchr(defstr, delim[1]) != NULL) )
  {
     token = strsep (&str, delim);
     col->red = ( atoi(token) * 65535 ) / 255;
     token = strsep (&str, delim);
     col->green = ( atoi(token) * 65535 ) / 255;
     token = strsep (&str, delim);
     col->blue = ( atoi(token) * 65535 ) / 255;

     return XAllocColor(display,
			DefaultColormap(display, DefaultScreen(display)),
			col);
  } else {
          return XAllocNamedColor(display,
			     DefaultColormap(display, DefaultScreen(display)),
			     defstr, col, &exact);
  }
}

#ifdef USE_XPM
void button_set_pixmap(button *b, char *filename)
{
  XpmAttributes attrib;
  //XGCValues gc_vals;
  //unsigned long valuemask = 0;

  b->pixmap = malloc(sizeof(Pixmap));
  b->mask = malloc(sizeof(Pixmap));

  attrib.valuemask = XpmCloseness;
  attrib.closeness = 40000;

  if (XpmReadFileToPixmap( b->kb->display, b->kb->win, filename,
		       b->pixmap, b->mask, &attrib)
      != XpmSuccess )
    {
          fprintf(stderr, "xkbd: failed loading image '%s'\n", filename);
	  exit(1);
    }

  /* we'll also be needing a gc for transparency */
  b->mask_gc = _createGC(b->kb->display,b->kb->win);
  /*
  gc_vals.clip_mask = *(b->mask);
  valuemask = GCClipMask;
  XChangeGC(b->kb->display, b->mask_gc, valuemask, &gc_vals);
  */
  XSetForeground(b->kb->display, b->mask_gc,
		 WhitePixel(b->kb->display, DefaultScreen(b->kb->display) ));
  XSetBackground(b->kb->display, b->mask_gc,
		 BlackPixel(b->kb->display, DefaultScreen(b->kb->display) ));

  XSetClipMask(b->kb->display, b->mask_gc, *(b->mask));

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

char *button_set(char *txt)
{
    char *res = malloc(sizeof(char)*(strlen(txt)+1));
    strcpy(res, txt);
    return res;
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
  if (kb->render_type == xft)
    {
      XGlyphInfo       extents;
      XftTextExtentsUtf8(kb->display, kb->xftfont,
			 (unsigned char *) txt, strlen(txt),
			 &extents);
      return extents.width;

    } else {
#endif
      return XTextWidth(kb->font_info, txt, strlen(txt));
#ifdef USE_XFT
    }
#endif
}

int _but_size(button *b, int l){
	int i,j,s;
	if (!b->txt_size[l]) for (i=0; i<STD_LEVELS; i++) {
		// set size=1 to empty as checked
		b->txt_size[i]=s=_button_get_txt_size(b->kb, b->txt[i])?:1;
		for (j=i+1; j<STD_LEVELS; j++) if (b->txt[i]==b->txt[j]) b->txt_size[j]=s;
	}
	return b->txt_size[l];
}

int button_calc_vwidth(button *b)
{
  if (b->vwidth ) return b->vwidth; /* already calculated from image or width_param */

//  b->vwidth = max3(
//	_button_get_txt_size(b->kb, DEFAULT_TXT(b)),
//	_button_get_txt_size(b->kb, SHIFT_TXT(b)),
//	_button_get_txt_size(b->kb, MOD_TXT(b))
//    );

  int i, sz = 0;
  for (i=0; i<STD_LEVELS; i++) if (b->txt) sz = max(sz,_but_size(b,i));
  b->vwidth = sz;
  return b->vwidth;
}

int button_calc_vheight(button *b)
{

  if (b->vheight) return b->vheight; /*already calculated from image or height param */

#ifdef USE_XFT
  if (b->kb->render_type == xft)
    {
      b->vheight =
	b->kb->xftfont->height;
    } else {
#endif
      b->vheight = b->kb->font_info->ascent + b->kb->font_info->descent;
#ifdef USE_XFT
    }
#endif
  return b->vheight;
}

int button_get_vheight(button *b)
{
   return b->vheight;
}

int button_set_b_size(button *b, int size)
{
   b->b_size = size;
   return size;
}

void button_render(button *b, int mode)
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

  int x,y,l;
  char *txt = NULL;
  int size = 0;

  x = button_get_abs_x(b) - b->kb->vbox->x;
  y = button_get_abs_y(b) - b->kb->vbox->y;

//  b->flags = (b->flags & ~(STATE(OBIT_PRESSED)|STATE(OBIT_LOCKED)|BUTTON_RELEASED))|mode;
  if (mode & STATE(OBIT_PRESSED))
    {
      gc_solid = b->fg_gc;
      if (no_lock) {
	gc_txt   = b->kb->txt_rev_gc;
#ifdef USE_XFT
	tmp_col  = b->col_rev;
#endif
     } else {
	gc_txt   = b->kb->txt_gc;
#ifdef USE_XFT
	tmp_col = b->col;
#endif
     }
    }
  else if(mode & STATE(OBIT_LOCKED))
    {
      gc_solid = b->fg_gc;
      gc_txt   = b->kb->txt_rev_gc;
#ifdef USE_XFT
      tmp_col  = b->col_rev;
#endif
    }
  else  /* BUTTON_RELEASED */
    {
      gc_solid = b->bg_gc;
      gc_txt   = b->kb->txt_gc;
#ifdef USE_XFT
      tmp_col = b->col;
#endif
    }


  /* figure out what text to display
     via keyboard state              */


  if (!(DEFAULT_KS(b) || SHIFT_KS(b) || MOD_KS(b)) &&
      ( DEFAULT_TXT(b) == NULL && SHIFT_TXT(b) == NULL && MOD_TXT(b) == NULL)
      )
    return;  /* its a 'blank' button - just a spacer */

  /* -- but color  gc1*/
  XFillRectangle( b->kb->display, b->kb->backing, gc_solid,
		  x, y, b->act_width, b->act_height );

  /* -- kb gc */
  if (b->kb->theme != plain)
    XDrawRectangle( b->kb->display, b->kb->backing, b->kb->bdr_gc,
		    x, y, b->act_width, b->act_height );

  if (b->kb->theme == rounded)
    {
      XDrawPoint( b->kb->display, b->kb->backing, b->kb->bdr_gc, x+1, y+1);
      XDrawPoint( b->kb->display, b->kb->backing,
		  b->kb->bdr_gc, x+b->act_width-1, y+1);
      XDrawPoint( b->kb->display, b->kb->backing,
		  b->kb->bdr_gc, x+1, y+b->act_height-1);
      XDrawPoint( b->kb->display, b->kb->backing,
		  b->kb->bdr_gc, x+b->act_width-1,
		  y+b->act_height-1);
    }

  if (b->pixmap != NULL)
    {
      /* TODO: improve alignment of images, kinda hacked at the mo ! */
      XGCValues gc_vals;
      unsigned long valuemask = 0;


      XSetClipMask(b->kb->display, b->mask_gc,*(b->mask));

      gc_vals.clip_x_origin = x+(b->x_pad/2)+b->b_size;
      gc_vals.clip_y_origin = y+b->vheight+(b->y_pad/2) +
	                              b->b_size-b->vheight +2;
      valuemask =  GCClipXOrigin | GCClipYOrigin ;
      XChangeGC(b->kb->display, b->mask_gc, valuemask, &gc_vals);

      XCopyArea(b->kb->display, *(b->pixmap), b->kb->backing, b->mask_gc,
		0, 0, b->vwidth,
		b->vheight, x+(b->x_pad/2)+b->b_size,
		y +b->vheight+(b->y_pad/2) -b->vheight + b->b_size+2
      );
      return; /* imgs cannot have text aswell ! */
    }

//  txt = GET_TXT(b,KBLEVEL(b))?:DEFAULT_TXT(b);
  l = KBLEVEL(b);
  txt = GET_TXT(b,l);
  if (!txt) {
	l = 0;
	txt = DEFAULT_TXT(b);
  }
 
  if (txt != NULL)
    {
       int xspace;
       //if (b->vwidth > _button_get_txt_size(b->kb,txt))
//       xspace = x+((b->act_width - _button_get_txt_size(b->kb,txt))/2);
      size=_but_size(b,l);
//      size=_button_get_txt_size(b->kb,txt);
       xspace = x+((b->act_width - size)>>1);
    
//       xspace = x+((b->act_width - b->vwidth)/2);
	  //else
	  //xspace = x+((b->vwidth)/2);
	  //xspace = x+(b->x_pad/2)+b->b_size;
#ifdef USE_XFT
    if (b->kb->render_type == xft)
      {
	int y_offset = ((b->vheight + b->y_pad) - b->kb->xftfont->height)/2;
	 XftDrawStringUtf8(b->kb->xftdraw, &tmp_col, b->kb->xftfont,
			/*x+(b->x_pad/2)+b->b_size, */
			xspace,
			/* y + b->vheight + b->b_size + (b->y_pad/2) - 4 */
			y + y_offset + b->kb->xftfont->ascent ,
		       /* y+b->vheight+(b->y_pad/2)-b->b_size, */
		       (unsigned char *) txt, strlen(txt));
      }
    else
#endif
      {
	XDrawString(
		    b->kb->display, b->kb->backing, gc_txt,
		    /*x+(b->x_pad/2)+b->b_size,*/
		    xspace,
		    y+b->vheight+(b->y_pad/2)+b->b_size
		    -4,
		    txt, strlen(txt)
		    );
      }
    }
}

void button_paint(button *b)
{
  /* use the vbox offsets for the location within the window */
  int x = button_get_abs_x(b) - b->kb->vbox->x;
  int y = button_get_abs_y(b) - b->kb->vbox->y;

  XCopyArea(b->kb->display, b->kb->backing, b->kb->win, b->kb->gc,
	    x, y, b->act_width, b->act_height,
	    x+b->kb->vbox->x, y+b->kb->vbox->y);
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
  /* total = total - b->kb->vbox->x;  HACK ! */

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
  button *b = calloc(1, sizeof(button));
  b->kb = k;

  b->fg_gc      = k->gc;
  b->bg_gc      = k->rev_gc;
  b->col = k->color;
  b->col_rev = k->color_rev;

  b->layout_switch = -1;

  return b;
}


