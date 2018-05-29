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
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>

#ifdef USE_XFT
#include <X11/Xft/Xft.h>
#endif
#include "structs.h"
#include "kb.h"
#include "box.h"
#include "button.h"


#ifndef MINIMAL
#include "ks2unicode.h"
#else
#define __ksText(ks) ""
#endif

#ifdef DEBUG
#define DBG(txt, args... ) fprintf(stderr, "DEBUG" txt "\n", ##args )
#else
#define DBG(txt, args... ) /* nothing */
#endif

static KeySym *keymap = NULL;
static int minkc = 0;
static int maxkc = 0;
static int ks_per_kc = 0;

static Bool
load_a_single_font(keyboard *kb, char *fontname )
{
#ifdef USE_XFT
  if ((kb->xftfont = XftFontOpenName(kb->display,
				     DefaultScreen(kb->display),
				     fontname)) != NULL)
    {
      return True;
    }
#else
  if ((kb->font_info = XLoadQueryFont(kb->display, fontname)) != NULL)
    {
      XSetFont(kb->display, kb->gc, kb->font_info->fid);
      return True;
    }
#endif
  return False;
}

void _kb_load_font(keyboard *kb, char *defstr )
{
  const char delim[] = "|";
  char *str, *token;

  if ((strchr(defstr, delim[0]) != NULL))
    {
      str = strdup(defstr);
      while( (token = strsep (&str, delim)) != NULL )
	  if (load_a_single_font(kb, token))
	    return;
    }
  else
    {
      if (load_a_single_font(kb, defstr )) return;
    }

  fprintf(stderr, "xkbd: unable to find suitable font in '%s'\n", defstr);
  exit(1);
}

void button_update(button *b) {
	int l,l1;
	int group = b->kb->total_layouts-1;
	Display *dpy = b->kb->display;
	char buf[1];
	const unsigned int mods[4] = {0,STATE(KBIT_SHIFT),STATE(KBIT_ALT),STATE(KBIT_SHIFT)|STATE(KBIT_ALT)};
	int n;
	char *txt;
	KeySym ks, ks1;
	KeyCode kc;
	unsigned int m;

	for(l=0; l<LEVELS; l++) {
		ks = b->ks[l];
		m = 0;
		if (l<4) {
			m = mods[l];
			if (!ks && (ks = b->ks[l&2]?:b->ks[0])) {
				XkbTranslateKeySym(dpy,&ks,m,buf,1,&n);
				b->ks[l] = ks;
			}
			if (!(txt = b->txt[l])) {
				for (l1 = 0; l1<l && !txt; l1++) if (ks == b->ks[l1]) txt = b->txt[l1];
#ifndef MINIMAL
				ksText_(ks,&txt);
#endif
				b->txt[l] = txt;
			}
#ifdef SLIDERS
		} else if (!ks && (ks = b->ks[l1=l&3])) {
			b->ks[l]=ks;
			b->kc[l]=b->kc[l1];
			m = b->mods[l1];
			if (!l1) m|=STATE(KBIT_CTRL);
			b->mods[l]=m;
			continue;
#endif
		}
		if (!ks) continue;
		ks1=ks;
		b->mods[l]=m;
		b->kc[l]=kc=XKeysymToKeycode(dpy,ks);
		if (!XkbLookupKeySym(dpy,kc,m,0,&ks1) || ks1!=ks){
			for(l1=0; l1<4; l1++) if ((m=mods[l1])!=b->mods[l]) {
				if (XkbLookupKeySym(dpy,kc,m,0,&ks1) && ks1==ks) {
					b->mods[l]=m;
					break;
				}
			}
		}
	}
#if 0
	unsigned int i ,j, p;
	n = maxkc-minkc+1;
	int m = -1;
	for (l=0; l<LEVELS; l++) {
	    if (!(ks = b->ks[l])) continue;
	    // as soon we have keymap - try to respect shift
	    if (keymap) {
		i = n;
		m = 4;
		for (m=0; m<4; m++) if (mods[m] == b->mods[l]) break;
		if (m<4) {
			j = (group<<1)|(m&1);
			for (i=0; i<n; i++)
				if (keymap[p=i*ks_per_kc+j] == ks) goto found;
			// try modifiers
			for (i=0; i<n; i++) for (j=4+(m&1); j<ks_per_kc; j+=2)
				if (keymap[p=i*ks_per_kc+j] == ks) goto found;
		};
		// everywere
		for (j=0; j<ks_per_kc; j++) for (i=0; i<n; i++)
			if (keymap[p=i*ks_per_kc+j] == ks) goto found;
found:		if (i<n) {
		    b->kc[l] = i+minkc;
		    continue;
		}
	    }
	    // if keymap wrong or not loaded
	    // 2do: Intrinsic.h - XtKeysymToKeycodeList
	    b->kc[l]=XKeysymToKeycode(dpy,ks);
	}
#endif
}

#ifdef SIBLINGS
void kb_find_button_siblings(button *b) {
	button *but = NULL, *tmp_but;
	box *vbox = ((box*)b->parent)->parent, *bx;
	list *listp, *ip;
	int bord = 1;
	int x1 = b->x + vbox->x;
	int y1 = b->y + vbox->y;
	int x2 = x1 + b->act_width;
	int y2 = y1 + b->act_height;
	int i;
	int n=0;
	x1 -= bord;
	y1 -= bord;
	x2 += bord;
	y2 += bord;
	button *buf[MAX_SIBLINGS];

	for (listp = vbox->root_kid; listp; listp = listp->next) {
		bx = (box *)listp->data;
		i=bx->y;
		if (y2<i) break;
		if (y1>i+bx->act_height) continue;
		for(ip=bx->root_kid; ip; ip= ip->next) {
			tmp_but = (button *)ip->data;
			i = tmp_but->x+bx->x;
			if (x2<i) break;
			if (x1>i+tmp_but->act_width) continue;
			if (n>=MAX_SIBLINGS) {
				fprintf(stderr,"too many siblings button in one point\n");
				break;
			}
			buf[n++] = tmp_but;
		}
	}
	if (b->siblings) free(b->siblings);
	b->nsiblings = n;
	n *= sizeof(button*);
	memcpy(b->siblings = malloc(n),buf,n);
}

void kb_update(keyboard *kb){
	list *listp, *ip;
	button *b;
	int i;

	for (listp = kb->vbox->root_kid; listp; listp = listp->next) {
		for(ip=((box *)listp->data)->root_kid; ip; ip= ip->next) {
			b = (button *)ip->data;
			if (b->siblings) return;
			kb_find_button_siblings(b);
		}
	}
}
#endif

int kb_load_keymap(Display *dpy) {
 KeySym *keymap1 = NULL;
 int minkc1,maxkc1,ks_per_kc1;
 int n = 0;

 XDisplayKeycodes(dpy, &minkc1, &maxkc1);
 keymap1 = XGetKeyboardMapping(dpy, minkc1, (maxkc1 - minkc1 + 1), &ks_per_kc1);

 if (!keymap || minkc1 != minkc || maxkc1 != maxkc || ks_per_kc1 != ks_per_kc ||
   memcmp(keymap, keymap1, (maxkc-minkc+1)*ks_per_kc1)) {
   if (keymap) free(keymap);
      keymap = keymap1;
      minkc = minkc1;
      maxkc = maxkc1;
      ks_per_kc = ks_per_kc1;
      return 1;
   }
   free(keymap1);
   /* keymap not changed */
   return 0;
}

#ifdef USE_XFT
#define _set_color_fg(kb,c,gc,xc)  __set_color_fg(kb,c,gc,xc)
void __set_color_fg(keyboard *kb, char *txt,GC *gc, XftColor *xc){
	XRenderColor colortmp;
#else
#define _set_color_fg(kb,c,gc,xc)  __set_color_fg(kb,c,gc)
void __set_color_fg(keyboard *kb, char *txt ,GC *gc){
#endif
	XColor col;
	Display *dpy = kb->display;
	if (_XColorFromStr(dpy, &col, txt) == 0) {
		perror("color allocation failed\n"); exit(1);
	}
#ifdef USE_XFT
	if (xc && kb->render_type == xft) {
		colortmp.red   = col.red;
		colortmp.green = col.green;
		colortmp.blue  = col.blue;
		colortmp.alpha = 0xFFFF;
		XftColorAllocValue(dpy,
			DefaultVisual(dpy,DefaultScreen(dpy)),
			DefaultColormap(dpy,DefaultScreen(dpy)),
			&colortmp, xc);
	} else
#endif
	{
		if (gc && !*gc) *gc = _createGC(dpy, kb->win);
		XSetForeground(dpy, *gc, col.pixel );
	}
}

box *_clone_box(box *vbox){
	box *b = malloc(sizeof(box));
	memcpy(b,vbox,sizeof(box));
	b->root_kid = b->tail_kid =NULL;
	return b;
}

box *clone_box(Display *dpy, box *vbox, int group){
	box *bx = _clone_box(vbox), *bx1;
	list *listp, *ip;
	box *tmp_box;
	button *b;
	int l,i;
	KeySym ks,ks1;
	KeyCode kc;

	for(listp = vbox->root_kid; listp; listp = listp->next) {
		box_add_box(bx, bx1=_clone_box((box *)listp->data));
		for (ip = ((box *)listp->data)->root_kid; ip; ip = ip->next) {
			memcpy(b=malloc(sizeof(button)),ip->data,sizeof(button));
			box_add_button(bx1,b);
			// new layout
			// in first look same code must be used to reconfigure 1 layout,
			// but no way to verify levels still equal in other definition.
			// this is paranoid case, but I keep restart way
			if (group)
			for(l=0; l<LEVELS; l++) {
				if (!(ks=b->ks[l]) || !b->txt[l] || (kc=b->kc[l])<minkc && kc>maxkc) continue;
				if (!(ks1=XkbKeycodeToKeysym(dpy, kc, 0, i=l&3)) || ks1 != ks)
				    for (i=0; i<100 && (ks1=XkbKeycodeToKeysym(dpy, kc, 0, i)) && ks1 != ks; i++) {}
				if (ks1 && ks1 == ks && (ks1=XkbKeycodeToKeysym(dpy, kc, group, i)) && ks1!=ks) {
					b->txt[l]=NULL;
					ksText_(b->ks[l]=ks1,&b->txt[l]);
				}
			}
		}
	}
	return bx;
}

void _simple_GC(keyboard *kb, GC *gc, int rev) {
	Display *dpy = kb->display;
	unsigned long b = BlackPixel(dpy, DefaultScreen(dpy));
	unsigned long w = WhitePixel(dpy, DefaultScreen(dpy));

	*gc = _createGC(dpy, kb->win);
	XSetForeground(dpy, *gc, rev?w:b);
	XSetBackground(dpy, *gc, rev?b:w);
}


keyboard* kb_new(Window win, Display *display, int kb_x, int kb_y,
		 int kb_width, int kb_height, char *conf_file,
		 char *font_name, int font_is_xft)
{
  keyboard *kb = NULL;

  int height_tmp = 0;

  list *listp;

  int max_width = 0; /* required for sizing code */
  //int cy = 0;        /* ditto                    */

  FILE *rcfp;
  char rcbuf[255];		/* buffer for conf file */
  char *tp;                     /* tmp pointer */

  char tmpstr_A[128];
  char tmpstr_C[128];

  box *tmp_box = NULL;
  button *tmp_but = NULL;
  int line_no = 0;
  enum { none, kbddef, rowdef, keydef } context;

  int font_loaded = 0;
  Colormap cmp;
  int max_single_char_width = 0;
  int max_single_char_height = 0;
  int j;

#ifdef USE_XFT
  XRenderColor colortmp;
#endif

  kb = calloc(1,sizeof(keyboard));
  kb->win = win;
  kb->display = display;

  cmp = DefaultColormap(display, DefaultScreen(display));

  /* create lots and lots of gc's */
  _simple_GC(kb,&kb->gc,0);
  _simple_GC(kb,&kb->rev_gc,1);
  _simple_GC(kb,&kb->txt_gc,0);
  _simple_GC(kb,&kb->txt_rev_gc,1);
  _simple_GC(kb,&kb->bdr_gc,0);

#ifdef USE_XFT
  kb->render_type = xft;
#else
  kb->render_type = oldskool;
#endif
  if (font_name != NULL)
    {
      if (font_is_xft)
      {
	 kb->render_type = xft;
      }
      _kb_load_font(kb, font_name );
      font_loaded = 1;
    }

#ifdef USE_XFT

  /* -- xft bits -------------------------------------------- */

  colortmp.red   = 0xFFFF;
  colortmp.green = 0xFFFF;
  colortmp.blue  = 0xFFFF;
  colortmp.alpha = 0xFFFF;
  XftColorAllocValue(display,
		     DefaultVisual(display, DefaultScreen(display)),
		     DefaultColormap(display,DefaultScreen(display)),
		     &colortmp,
		     &kb->color_bg);

  colortmp.red   = 0x0000;
  colortmp.green = 0x0000;
  colortmp.blue  = 0x0000;
  colortmp.alpha = 0xFFFF;
  XftColorAllocValue(display,
		     DefaultVisual(display, DefaultScreen(display)),
		     DefaultColormap(display,DefaultScreen(display)),
		     &colortmp,
		     &kb->color_fg);

  /* --- end xft bits -------------------------- */

  kb->xftdraw = NULL;
#endif

  /* Defaults */
  kb->theme            = rounded;
  kb->key_delay_repeat = 50;
  kb->key_repeat       = -1;

  kb->total_layouts = 0;


  kb->backing = 0;

  kb_load_keymap(display);

  if (!strcmp(conf_file,"-")) {
     rcfp = stdin;
  } else if ((rcfp = fopen(conf_file, "r")) == NULL) {
      fprintf(stderr, "xkbd: Cant open conf file: %s\n", conf_file);
      exit(1);
  }

  context = none;

  while(fgets(rcbuf, sizeof(rcbuf), rcfp) != NULL)
    {
      tp = &rcbuf[0];

      /* strip init spaces */
      while(*tp == ' ' || *tp == '\t') tp++;

      /* ignore comments and blank lines */
      if (*tp == '\n' || *tp == '#') { DBG("Config: got hash\n"); continue; }

      if (*tp == '<') /* a 'tag' - set the context */
	{
	  if (*(tp+1) == '/') /* closing tag */
	    {
	      switch (context) {
		case kbddef:
		  if (!font_loaded)
		    _kb_load_font(kb, "fixed" );
		  break;
		case rowdef:

		  break;
		case keydef:
		  button_update(tmp_but);
		  button_calc_c_width(tmp_but);
		  button_calc_c_height(tmp_but);
		  break;
		case none:
		  break;
	      }
	      context = none;
	      continue;
	    }
	  if (sscanf(tp, "<%s", tmpstr_A) == 1)  /* get tag name */
	    {
	      if (strncmp(tmpstr_A, "global", 5) == 0)
		{
		  context=kbddef;
		  continue;
		}
	      if (strncmp(tmpstr_A, "layout", 6) == 0)
		{
		  kb->total_layouts++;
		  kb->kbd_layouts[kb->total_layouts-1] = box_new();
		  kb->vbox = kb->kbd_layouts[kb->group = kb->total_layouts-1];
		  kb->vbox->act_width  = kb_width;
		  kb->vbox->act_height = kb_height;
		  kb->vbox->min_height = 0;
		  kb->vbox->min_width = 0;
		  kb->vbox->x = kb_x;
		  kb->vbox->y = kb_y;
		  continue;
		}
	      if (strncmp(tmpstr_A, "row", 3) == 0)
		{
		  if (kb->total_layouts == 0)
		    {
		      /*
			 Minor Kludge :-)
			 So older configs work we can work with
			 out a <layout> tag
		      */
		      kb->total_layouts++;
		      kb->kbd_layouts[kb->total_layouts-1] = box_new();
		      kb->vbox = kb->kbd_layouts[kb->group = kb->total_layouts-1];
		      kb->vbox->act_width  = kb_width;
		      kb->vbox->act_height = kb_height;
		      kb->vbox->min_height = 0;
		      kb->vbox->min_width = 0;
		      kb->vbox->x = kb_x;
		      kb->vbox->y = kb_y;
		    }
		  context=rowdef;
		  tmp_box = box_add_box(kb->vbox, box_new());

		  continue;
		}
	      if (strncmp(tmpstr_A, "key", 3) == 0)
		{
		  /* check here for NULL tmp_button */
		  tmp_but = box_add_button(tmp_box, button_new(kb) );
		  context=keydef;
		  continue;
		}

	    } else {
	      fprintf(stderr,"Config file parse failed (tag) at line: %i\n",
		      line_no);
	      exit(1);
	    }
	}
      else             /* a key=value setting */
	{
	  if (sscanf(tp, "%s %s", tmpstr_A,tmpstr_C) == 2) {

	    switch (context) {
	      case none:
		break;
	      case kbddef: /*
		if (strcmp(tmpstr_A, "render") == 0)
		  {
		    if ((strncmp(tmpstr_C, "xft", 3) == 0)
			&& !font_loaded)
		      {
			kb->render_type = xft;
		      }
		  }
		else
			   */
		if ((strcmp(tmpstr_A, "font") == 0)
			 && !font_loaded)
		  {
		     _kb_load_font(kb,tmpstr_C );
		     font_loaded=1;
		  }
		else if (strcmp(tmpstr_A, "button_style") == 0)
		  {
		    if (strcmp(tmpstr_C, "square") == 0)
		      kb->theme = square;
		    else if (strcmp(tmpstr_C, "plain") == 0)
		      kb->theme = plain;
		  }
		else if (strcmp(tmpstr_A, "col") == 0)
		  _set_color_fg(kb,tmpstr_C,&kb->rev_gc,NULL);
		else if (strcmp(tmpstr_A, "border_col") == 0)
		  _set_color_fg(kb,tmpstr_C,&kb->bdr_gc,NULL);
		else if (strcmp(tmpstr_A, "down_col") == 0)
		  _set_color_fg(kb,tmpstr_C,&kb->gc,NULL);
		else if (strcmp(tmpstr_A, "width") == 0)
		  {
		    /* TODO fix! seg's as kb->vbox does not yet exist
		     if (!kb->vbox->act_width)
			kb->vbox->act_width = atoi(tmpstr_C);
		    */
		  }
		else if (strcmp(tmpstr_A, "height") == 0)
		    /* TODO fix! seg's as kb->vbox does not yet exist
		     if (!kb->vbox->act_height)
			kb->vbox->act_height = atoi(tmpstr_C);
		    */
                    height_tmp = atoi(tmpstr_C);
#ifdef SLIDES
		else if (strcmp(tmpstr_A, "slide_margin") == 0)
		     kb->slide_margin = atoi(tmpstr_C);
#endif
		else if (strcmp(tmpstr_A, "repeat_delay") == 0)
		     kb->key_delay_repeat = atoi(tmpstr_C);
		else if (strcmp(tmpstr_A, "repeat_time") == 0)
		     kb->key_repeat = atoi(tmpstr_C);
		else if (strcmp(tmpstr_A, "txt_col") == 0)
		     _set_color_fg(kb,tmpstr_C,&kb->txt_gc,&kb->color_fg);
		break;
	      case rowdef: /* no rowdefs as yet */
		break;
	      case keydef:
		if (strcmp(tmpstr_A, "default") == 0)
		  DEFAULT_TXT(tmp_but) = button_set(tmpstr_C);
		else if (strcmp(tmpstr_A, "shift") == 0)
		  SHIFT_TXT(tmp_but) = button_set(tmpstr_C);
		else if (strcmp(tmpstr_A, "switch") == 0) {
		  // -1 is default, but setting in config -> -2 & switch KeySym
		  // to other keysym - set -2 & concrete default_ks
		  if((tmp_but->layout_switch = atoi(tmpstr_C))==-1) {
		    button_set_txt_ks(tmp_but, "ISO_Next_Group");
		    tmp_but->layout_switch = -2;
		  }
		} else if (strcmp(tmpstr_A, "mod") == 0)
		  MOD_TXT(tmp_but) = button_set(tmpstr_C);
		else if (strcmp(tmpstr_A, "shift_mod") == 0)
		  SHIFT_MOD_TXT(tmp_but) = button_set(tmpstr_C);
		else if (strcmp(tmpstr_A, "default_ks") == 0)
		  button_set_txt_ks(tmp_but, tmpstr_C);
		else if (strcmp(tmpstr_A, "shift_ks") == 0)
		  SET_KS(tmp_but,1,button_ks(tmpstr_C))
		else if (strcmp(tmpstr_A, "mod_ks") == 0)
		  SET_KS(tmp_but,2,button_ks(tmpstr_C))
		else if (strcmp(tmpstr_A, "shift_mod_ks") == 0)
		  SET_KS(tmp_but,3,button_ks(tmpstr_C))
#ifdef USE_XPM
		else if (strcmp(tmpstr_A, "img") == 0)
		  { button_set_pixmap(tmp_but, tmpstr_C); }
#endif
		else if (strcmp(tmpstr_A, "bg") == 0)
		  {tmp_but->bg_gc=NULL; _set_color_fg(kb,tmpstr_C,&tmp_but->bg_gc,NULL);}
		else if (strcmp(tmpstr_A, "fg") == 0)
		  {tmp_but->fg_gc=NULL; _set_color_fg(kb,tmpstr_C,&tmp_but->fg_gc,NULL);}
		else if (strcmp(tmpstr_A, "slide_up_ks") == 0)
		  button_set_slide_ks(tmp_but, tmpstr_C, UP);
		else if (strcmp(tmpstr_A, "slide_down_ks") == 0)
		  button_set_slide_ks(tmp_but, tmpstr_C, DOWN);
		else if (strcmp(tmpstr_A, "slide_left_ks") == 0)
		  button_set_slide_ks(tmp_but, tmpstr_C, LEFT);
		else if (strcmp(tmpstr_A, "slide_right_ks") == 0)
		  button_set_slide_ks(tmp_but, tmpstr_C, RIGHT);
		else if (strcmp(tmpstr_A, "width") == 0)
		{
		   tmp_but->flags |= STATE(OBIT_WIDTH_SPEC);
		    tmp_but->c_width = atoi(tmpstr_C);
		}
		else if (strcmp(tmpstr_A, "key_span_width") == 0)
		{
		   tmp_but->flags |= STATE(OBIT_WIDTH_SPEC);
		   tmp_but->key_span_width = atoi(tmpstr_C);
		}

		else if (strcmp(tmpstr_A, "height") == 0)
		  tmp_but->c_height = atoi(tmpstr_C);
                else if (strcmp(tmpstr_A, "obey_capslock") == 0)
		{
		  if (strcmp(tmpstr_C, "yes") == 0)
		    tmp_but->flags |= STATE(OBIT_OBEYCAPS);
		  else if (strcmp(tmpstr_C, "no") == 0)
		    tmp_but->flags &= ~STATE(OBIT_OBEYCAPS);
		  else
		  {
		    perror("invalid value for obey_capslock\n"); exit(1);
		  }
		}
		break;
	    }

	  } else {
	     fprintf(stderr,"Config file parse failed at line: %i\n",
		     line_no);
	     exit(1);
	  }
	}
      line_no++;
    }

  fclose(rcfp);

  kb->key_delay_repeat1 = kb->key_delay_repeat;
  kb->key_repeat1 = kb->key_repeat;

  kb->vbox = kb->kbd_layouts[kb->group = 0];
  if(height_tmp)
    kb->vbox->act_height = height_tmp;
  /* pass 1 - calculate min dimentions  */

  listp = kb->vbox->root_kid;


  /* find the max single char width */

  while (listp != NULL)
    {
      list *ip;
      tmp_box = (box *)listp->data;
      ip = tmp_box->root_kid;

      while (ip != NULL)
	{
	   button *b;
	   b = (button *)ip->data;
	   if (!(b->flags & STATE(OBIT_WIDTH_SPEC)))
	   {
	      if ( ( DEFAULT_TXT(b) == NULL || (strlen(DEFAULT_TXT(b)) == 1))
		   && (SHIFT_TXT(b) == NULL || (strlen(SHIFT_TXT(b)) == 1))
		   && (MOD_TXT(b) == NULL || (strlen(MOD_TXT(b)) == 1))
		     && b->pixmap == NULL
		   )
	      {
		 if (b->c_width > max_single_char_width)
		    max_single_char_width = b->c_width;

	      }
	      if (b->c_height > max_single_char_height)
		 max_single_char_height = b->c_height;

	   }

	  ip = ip->next;
	}
      listp = listp->next;
    }

  /* Set all single char widths to the max one, figure out minimum sizes */

  max_single_char_height += 2;

  for(j=0;j<kb->total_layouts;j++)
    {
      kb->vbox = kb->kbd_layouts[j];
      listp = kb->vbox->root_kid;

      while (listp != NULL)
	{
	  list *ip;
	  int tmp_width = 0;
	  int tmp_height = 0;
	  int max_height = 0;

	  tmp_box = (box *)listp->data;
	  ip = tmp_box->root_kid;

	  while (ip != NULL)
	    {
	      button *b;
	      b = (button *)ip->data;
	      if (!(b->flags & STATE(OBIT_WIDTH_SPEC)))
		{
		  if ((DEFAULT_TXT(b) == NULL || (strlen(DEFAULT_TXT(b)) == 1))
		      && (SHIFT_TXT(b) == NULL || (strlen(SHIFT_TXT(b)) == 1))
		      && (MOD_TXT(b) == NULL || (strlen(MOD_TXT(b)) == 1))
		      && b->pixmap == NULL )
		    {
		      b->c_width = max_single_char_width;
		    }
		}
	      b->c_height = max_single_char_height;
	      //printf("width is %i\n", b->c_width);
	      if (b->key_span_width)
		b->c_width = b->key_span_width * max_single_char_width;

	      tmp_width += ( ((button *)ip->data)->c_width +
			     (((button *)ip->data)->b_size*2) );
	      tmp_height = ( ((button *)ip->data)->c_height +
			     (((button *)ip->data)->b_size*2));
	      if (tmp_height >= max_height) max_height = tmp_height;
	      ip = ip->next;
	    }
	  if (tmp_width > max_width) max_width = tmp_width;
	  if (tmp_height >= max_height) max_height = tmp_height;
	  tmp_box->min_width  = tmp_width;
	  tmp_box->min_height = max_height;
	  kb->vbox->min_height += max_height; // +1;



	  listp = listp->next;
	}
	  if ((j > 0) && kb->vbox->min_height > kb->kbd_layouts[0]->min_height)
	    kb->kbd_layouts[0]->min_height = kb->vbox->min_height;

      kb->vbox->min_width = max_width;
    }

  /* TODO: copy all temp vboxs  */


  kb->vbox = kb->kbd_layouts[kb->group = 0];

  return kb;

}

void kb_size(keyboard *kb)
{

   /* let the fun begin :) */
   list *listp;
   int cy = 0;
   box *tmp_box = NULL;

   if ( kb->vbox->act_width == 0)
      kb->vbox->act_width = kb->vbox->min_width ; /* by default add a
						     little to this on init */
   if ( kb->vbox->act_height == 0)
      kb->vbox->act_height = kb->vbox->min_height ;

   if (kb->backing != None)
      XFreePixmap(kb->display, kb->backing);

   kb->backing = XCreatePixmap(kb->display, kb->win,
			       kb->vbox->act_width, kb->vbox->act_height,
			       DefaultDepth(kb->display,
		                            DefaultScreen(kb->display)) );

   XFillRectangle(kb->display, kb->backing,
		  kb->rev_gc, 0, 0,
		  kb->vbox->act_width, kb->vbox->act_height);

#ifdef USE_XFT
   if (kb->xftdraw != NULL) XftDrawDestroy(kb->xftdraw);


   kb->xftdraw = XftDrawCreate(kb->display, (Drawable) kb->backing,
			       DefaultVisual(kb->display,
					     DefaultScreen(kb->display)),
			       DefaultColormap(kb->display,
					       DefaultScreen(kb->display)));
#endif


  listp = kb->vbox->root_kid;
  while (listp != NULL)
    {
      list *ip;
      int cx = 0;
      //int total = 0;
      int y_pad = 0;
      button *tmp_but = NULL;
      tmp_box = (box *)listp->data;
      tmp_box->y = cy;
      tmp_box->x = 0;
      ip = tmp_box->root_kid;
      y_pad =  (int)(
		     ( (float)(tmp_box->min_height)/kb->vbox->min_height )
		     * kb->vbox->act_height );


      while (ip != NULL)
	{
	  int but_total_width;

	  tmp_but = (button *)ip->data;

	  tmp_but->x = cx; /*remember relative to holding box ! */

	  but_total_width = tmp_but->c_width+(2*tmp_but->b_size);

	  tmp_but->x_pad = (int)(((float)but_total_width/tmp_box->min_width)
	    * kb->vbox->act_width);

	  tmp_but->x_pad -= but_total_width;

	  tmp_but->act_width = tmp_but->c_width + tmp_but->x_pad
	                       + (2*tmp_but->b_size);

	  cx += (tmp_but->act_width );

	  tmp_but->y = 0;
	  tmp_but->y_pad = y_pad - tmp_but->c_height - (2*tmp_but->b_size);
	  tmp_but->act_height = y_pad;
	  ip = ip->next;

	  /*  hack for using all screen space */
	  if (listp->next == NULL) tmp_but->act_height--;

	}

      /*  another hack for using up all space */
      tmp_but->x_pad += (kb->vbox->act_width-cx) -1 ;
      tmp_but->act_width += (kb->vbox->act_width-cx) -1;

      cy += y_pad ; //+ 1;
      tmp_box->act_height = y_pad;
      tmp_box->act_width = kb->vbox->act_width;

      listp = listp->next;

    }
#ifdef SIBLINGS
    kb_update(kb);
#endif
}

void
kb_switch_layout(keyboard *kb, int kbd_layout_num)
{
  int w = kb->vbox->act_width;
  int h = kb->vbox->act_height;
  int mw = kb->vbox->min_width;
  int mh = kb->vbox->min_height;

  for(; kbd_layout_num >= kb->total_layouts; kb->total_layouts++)
    kb->kbd_layouts[kb->total_layouts] = clone_box(kb->display,kb->kbd_layouts[0],kb->total_layouts);

  kb->vbox = kb->kbd_layouts[kb->group = kbd_layout_num];

  kb->vbox->act_width = w;
  kb->vbox->act_height = h;
  kb->vbox->min_width = mw;
  kb->vbox->min_height = mh;

  kb_size(kb);
  kb_render(kb);
  kb_paint(kb);
}

void kb_render(keyboard *kb)
{
	list *listp = kb->vbox->root_kid;
	box *tmp_box;
	while (listp != NULL) {
		list *ip = ((box *)listp->data)->root_kid;
		while (ip != NULL) {
			button *tmp_but = (button *)ip->data;
			button_render(tmp_but, (tmp_but->modifier & kb->state_locked)?STATE(OBIT_LOCKED):(tmp_but->modifier & kb->state)?STATE(OBIT_PRESSED):0);
			ip = ip->next;
		}
		listp = listp->next;
	}
}

void kb_paint(keyboard *kb)
{
  XCopyArea(kb->display, kb->backing, kb->win, kb->gc,
	    0, 0, kb->vbox->act_width, kb->vbox->act_height,
	    kb->vbox->x, kb->vbox->y);
}

void _release(button *b){
#ifdef MULTITOUCH
	if (b->flags & STATE(OBIT_PRESSED)) {
		kb_process_keypress(b,0,STATE(OBIT_UGLY));
		return;
	}
#endif
	button_render(b, 0);
	button_paint(b);

}

void _press(button *b, unsigned int flags){
	flags |= STATE(OBIT_PRESSED);
#ifdef MULTITOUCH
	if (b->modifier & (KB_STATE_KNOWN ^ STATE(KBIT_CAPS))) {
		kb_process_keypress(b,0,flags);
		return;
	}
#endif
	button_render(b, flags);
	button_paint(b);

}

button *kb_handle_events(keyboard *kb, int type, int x, int y, uint32_t ptr, Time time)
{
	button *b;
	int i,j;
	Time T;

	static uint32_t touchid[MAX_TOUCH] = {};
	static button *but[MAX_TOUCH] = {};
	static Time times[MAX_TOUCH] = {};
	static int X[MAX_TOUCH];
	static int Y[MAX_TOUCH];
#ifdef SIBLINGS
	static button *sib[MAX_TOUCH][MAX_SIBLINGS];
	static short nsib[MAX_TOUCH];
#endif

#ifdef MULTITOUCH
	int t=-1;
	// find touch
	for (i=0; i<MAX_TOUCH; i++) {
		if (touchid[i] == ptr) {
			// duplicate (I got BEGIN!)
//			if (time == (T=times[i]) && x==X[i] && y==Y[i]) return but[i];
			if (time<=times[i] && type!=2) return but[i];
			t=i;
			break;
		}
	}
#else
	const int t=0;
#endif
	if (!type) { // BEGIN/press
		b = kb_find_button(kb,x,y);
		if (!b) return NULL;
#ifdef MULTITOUCH
		// [kick oldest] touch, start to track
		if (T=times[t=0]) for (i=1; i<MAX_TOUCH; i++) if (T>times[i] && !(T=times[t=i])) break;
#endif
		if (but[t]) _release(but[t]);
		but[t] = b;
		touchid[t] = ptr;
		times[t] = time;
		X[t] = x;
		Y[t] = y;
#ifdef SIBLINGS
		nsib[t] = -1;
#endif
		_press(b,0);
		return b;
	}

#ifdef MULTITOUCH
	if (t<0) {
//		fprintf(stderr,"untracked touch on state %i\n",type);
		return NULL;
	}
#endif

	b = kb_find_button(kb,x,y);
	// simple logic: no button change in one touch
	if (b != but[t]) {
#ifdef SIBLINGS
		/*
		    intersection of sets of sibling buttons
		    previous button or intersection with new button.
		    if set size is 1 - this is result of touch.
		    else must do other selections.
		*/
		if (but[t]) {
			if (!b) goto drop; // button -> NULL
			// button -> button
			int ns = nsib[t], ns1 = b->nsiblings;
			button **s1 = (button **)b->siblings;
			button *b1 = NULL;
			int n=0;
			if (ns<0) {
				button **s = (button **)but[t]->siblings;
				nsib[t] = ns = but[t]->nsiblings;
				for(i=0; i<ns; i++)
				    for(j=0; j<ns1; j++)
					if (s[i]==s1[j]) {
					    sib[t][n++]=s1[j];
					    break;
					}
			} else {
				for(i=0; i<ns; i++)
				    for(j=0; j<ns1; j++)
					if (sib[t][i]==s1[j]) {
					    sib[t][n++]=s1[j];
					    break;
					}
			}
			nsib[t]=n;
			if (n==0) { // no siblings, drop touch
				goto drop;
			}else if (n==1) { // 1 intersection: found button
				b1 = sib[t][0];
			// unknown, need to calculate or touch more
			}else {
#if 0
				if (type==2) goto drop;
#else
				// try set of 2 last buttons
				// unsure in this logic, but there are
				// last chance to 1) draw 2) press something
				button *s2[2] = { but[t], b };
				button *s3[2];
				int n1=0;
				for(j=0; j<2; j++)
				    for(i=0; i<n; i++)
					if (sib[t][i]==s2[j]) {
					    s3[n1++]=s2[j];
					    break;
					}
				if (n1==1) b1 = sib[t][0];
				else if (type==2) goto drop;
#endif
			}
			if (b1) {
				if (b1!=but[t]) {
					_release(b1);
					_press(but[t],STATE(OBIT_UGLY));
				}
				b = b1;
			} else { // multiple: can draw whole set or nothing
				b1 = but[t];
#if 0
				// incomplete. need to release
				for(i=0; i<n; i++){
					_press(sib[t][i],STATE(OBIT_UGLY));
					if (sib[t][i]==b1) b1 = NULL;
				}
				if (b1)
#endif
					_release(b1);
			}
			but[t]=b;
		} else if (b) { // NULL -> button: new siblings base list
			nsib[t]=-1;
		}
#else
		if (but[t]) goto drop;
#endif
	}

	if (type!=2){ // motion/to be continued
	    if (b) {
		times[t] = time;
		X[t] = x;
		Y[t] = y;
	    }
	    return b;
	}

	// the END/release
	if (!b) goto drop;
#ifdef SLIDES
	kb_set_slide(b, x, y );
#endif
	kb_process_keypress(b,0,0);
	if (b->modifier || b->layout_switch > -1) but[t] = NULL;
drop:
	if (b=but[t]) {
		but[t] = NULL;
		_release(b);
#ifdef SLIDES
		b->slide = 0;
#endif
	}
	touchid[t] = 0;
	times[t] = 0;
	return NULL;
}

#ifdef SLIDES
void kb_set_slide(button *b, int x, int y)
{
  if (x < (button_get_abs_x(b)-b->kb->slide_margin))
    { b->slide = SLIDE_LEFT; return; }

  if (x > ( button_get_abs_x(b)
	    + b->act_width + -b->kb->slide_margin ))
    { b->slide = SLIDE_RIGHT; return; }

  if (y < (button_get_abs_y(b)-b->kb->slide_margin))
    { b->slide = SLIDE_UP; return; }

  if (y > ( button_get_abs_y(b) + b->act_height )
      + -b->kb->slide_margin )
    { b->slide = SLIDE_DOWN; return; }


}
#endif

Bool kb_do_repeat(keyboard *kb, button *b)
{
  static int timer;
  static Bool delay;

  if (!kb->key_repeat)
    return False;
  if (b == NULL)
    {
      timer = 0;
      delay = False;
      return False; /* reset everything */
    }
  /* sometimes update rate from X */
  if (!timer++ && kb->key_repeat == -1) {
	unsigned int d, r;

	XkbGetAutoRepeatRate(kb->display, XkbUseCoreKbd, &d, &r);
	kb->key_delay_repeat1 = d / 10 + 1;
	kb->key_repeat1 = r / 10 + 1;
  }
  if ((delay && timer == kb->key_repeat1)
      || (!delay && timer == kb->key_delay_repeat1))
    {
      kb_process_keypress(b, 1,0);
      timer = 0;
      delay = True;
    }
    return True;
}

static unsigned int state_used = 0;
void kb_process_keypress(button *b, int repeat, unsigned int flags)
{
    keyboard *kb = b->kb;
    unsigned int state = kb->state;
    unsigned int lock = kb->state_locked;
    const unsigned int mod = b->modifier;
    int keypress = 1;
    int st;
#ifndef MINIMAL
    Display *dpy = kb->display;
#endif

    DBG("got release state %i %i %i %i \n", state, STATE(KBIT_SHIFT), STATE(KBIT_MOD), STATE(KBIT_CTRL) );

    if (repeat) {
    } else if (mod) {
	keypress = 0;
	if (flags & STATE(OBIT_PRESSED)){
		b->flags |= flags;
		if ((lock|state) & mod) state_used |= mod;
		else state_used &= ~mod;
		if (no_lock || ~(state|lock) & mod) {
			lock |= mod;
			state |= mod;
		}
	} else if (b->flags & STATE(OBIT_PRESSED)) {
		if ((flags|b->flags) & STATE(OBIT_UGLY)) {
			b->flags &= ~(STATE(OBIT_PRESSED)|STATE(OBIT_UGLY));
			state &= ~mod;
			lock &= ~mod;
			state_used &= ~mod;
		} else {
			b->flags &= ~STATE(OBIT_PRESSED);
			if (state_used & mod) {
				state &= ~mod;
				state_used &= ~mod;
			}
			if (no_lock) lock &= ~mod;
			else lock ^= mod;
		}
	} else if (lock & mod) {
		lock ^= mod;
		state ^= mod;
	} else if (!(state & mod)) {
		state |= mod;
		if (mod & STATE(KBIT_CAPS)) lock |= mod;
	} else if (no_lock) {
		state ^= mod;
	} else {
		lock ^= mod;
	}
	DBG("got a modifier key - %i \n", state);
    } else {
	state_used |= state|lock;
	state = lock;
	DBG("kbd is shifted, unshifting - %i \n", state);
    }

    st = (state|lock) & KB_STATE_KNOWN;

    if (keypress) {
	fprintf(stderr,"");
	kb_send_keypress(b, st, flags);
	DBG("%s clicked \n", DEFAULT_TXT(b));
    }
    if (state != kb->state || lock != kb->state_locked) {
#ifndef MINIMAL
	if (Xkb_sync) {
		XSync(dpy,False); // serialize
		if (st != kb->state & KB_STATE_KNOWN) XkbLatchModifiers(dpy,XkbUseCoreKbd,KB_STATE_KNOWN,st);
		if (st != kb->state_locked & KB_STATE_KNOWN) XkbLockModifiers(dpy,XkbUseCoreKbd,KB_STATE_KNOWN,st);
//		if ((state^kb->state ^ state)&KB_STATE_KNOWN) XkbLatchModifiers(dpy,XkbUseCoreKbd,KB_STATE_KNOWN,state & KB_STATE_KNOWN);
//		if ((lock^kb->state_locked ^ lock)&KB_STATE_KNOWN) XkbLockModifiers(dpy,XkbUseCoreKbd,KB_STATE_KNOWN,lock & KB_STATE_KNOWN);
		XSync(dpy,True); // reduce events
	}
#endif
	kb->state = state;
	kb->state_locked = lock;
	kb_render(kb);
	kb_paint(kb);
   }
   if (b->layout_switch!=-1) {
    if (b->layout_switch>-1) {
#ifndef MINIMAL
	if (Xkb_sync) {
		XSync(dpy,False);
		XkbLockGroup(dpy, XkbUseCoreKbd, b->layout_switch);
		XSync(dpy,True);
	}
#endif
	kb_switch_layout(kb, b->layout_switch);
    } else {
        XkbStateRec s;
	XkbGetState(dpy,XkbUseCoreKbd,&s);
	kb_switch_layout(kb, s.group);
    }
   }
}

#ifndef MINIMAL
#define _xsync(x) if (Xkb_sync) XSync(dpy,x)
#else
#define _xsync(x)
#endif

static int saved_mods = 0;
void kb_send_keypress(button *b, unsigned int next_state, unsigned int flags) {
#ifdef SLIDES
	unsigned int l = b->slide?:KBLEVEL(b);
#else
	unsigned int l = KBLEVEL(b);
#endif
	unsigned int mods = b->mods[l];
	Display *dpy = b->kb->display;
	unsigned int mods0 = b->kb->state_locked & KB_STATE_KNOWN;

#ifndef MINIMAL
	if (!Xkb_sync) 
#endif
	{
		mods0 = saved_mods;
		saved_mods = next_state = mods;
	}
	
	if (b->kc[l]<minkc || b->kc[l]>maxkc) return;
	if ((mods0 ^ mods) & STATE(KBIT_CAPS)) mods ^= STATE(KBIT_CAPS)|STATE(KBIT_SHIFT);
//	if ((mods0 ^ mods) & STATE(KBIT_MOD)) mods ^= STATE(KBIT_MOD)|STATE(KBIT_ALT);
	_xsync(False);
	if (mods != mods0) {
		XkbLockModifiers(dpy,XkbUseCoreKbd,KB_STATE_KNOWN,mods);
		_xsync(True);
	} else mods = b->kb->state;
	if (!(b->flags & STATE(OBIT_PRESSED))) {
		XTestFakeKeyEvent(b->kb->display, b->kc[l], True, 0);
		_xsync(True);
		b->flags |= STATE(OBIT_PRESSED);
	}
	if (!(flags & STATE(OBIT_PRESSED))) {
		XTestFakeKeyEvent(b->kb->display, b->kc[l], False, 0);
		_xsync(True);
		b->flags &= ~STATE(OBIT_PRESSED);
	}
	if (mods != next_state && next_state == mods0) {
		XkbLockModifiers(dpy,XkbUseCoreKbd,KB_STATE_KNOWN,mods0);
		_xsync(True);
	}
}

button * kb_find_button(keyboard *kb, int x, int y)
{
	button *but = NULL, *tmp_but;
	box *vbox = kb->vbox, *bx;
	list *listp, *ip;
	int i;

	x -= vbox->x;
	y -= vbox->y;

	for (listp = vbox->root_kid; listp; listp = listp->next) {
		bx = (box *)listp->data;
		i = bx->y;
		if (y < i) break;
		if (y > i+bx->act_height) continue;
		for(ip=bx->root_kid; ip; ip= ip->next) {
			tmp_but = (button *)ip->data;
			i = tmp_but->x+bx->x;
			if (x<i) break;
			if (x>i+tmp_but->act_width) continue;
//			i = tmp_but->y+bx->y
//			if (y<i) break;
//			if (y>i+tmp_but->act_height) continue;
			/* if pressed invariant/border - check buttons are identical */
			if (but && memcmp(but,tmp_but,(char*)&but->flags - (char*)&but->ks) + sizeof(but->flags)) return NULL;
			but = tmp_but;
		}
	}
	if (!but) fprintf(stderr, "xkbd: no button %i,%i\n",x,y);
	return but;
}



void kb_destroy(keyboard *kb)
{
  XFreeGC(kb->display, kb->gc);
  /* -- to do -- somwthing like this
  while (listp != NULL)
    {


      button *tmp_but = NULL;
      tmp_box = (box *)listp->data;
      box_destroy(tmp_box) -- this will destroy the buttons

    }
  */

  free(kb);
}
