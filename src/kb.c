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
static int notkc = 0;
static int ks_per_kc = 0;


static Bool
load_a_single_font(keyboard *kb, char *fontname )
{
#ifdef USE_XFT
  if (kb->xftfont) XftFontClose(kb->display, kb->xftfont);

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

char *loaded_font = NULL;
void _kb_load_font(keyboard *kb, char *font)
{
  const char delim[] = "|";
  char *token;



  if (loaded_font && strcmp(loaded_font,font)) return;
  loaded_font = strdup(font);
  if ((strchr(loaded_font, delim[0]) != NULL)) {
      while( (token = strsep (&loaded_font, delim)) != NULL )
	  if (load_a_single_font(kb, token)) return;
  } else if (load_a_single_font(kb, loaded_font)) return;

  fprintf(stderr, "xkbd: unable to find suitable font in '%s'\n", font);
  exit(1);
}

void button_update(button *b) {
	int l,l1;
	int group = b->kb->total_layouts-1;
	Display *dpy = b->kb->display;
	char buf[1];
	int n;
	char *txt;
	KeySym ks, ks1;
	KeyCode kc;
	unsigned int m;
	int is_sym = 0;

	for(l=0; l<LEVELS; l++) {
		b->kc[l] = notkc;
		ks = b->ks[l];
		m = 0;
		if (!ks && l && l<STD_LEVELS && (
		    ( (l1=l&~1U) && b->ks[l1] &&
			(kc=b->kc[l1])>=minkc && kc<=maxkc &&
			(ks=XkbKeycodeToKeysym(dpy, kc, group, l))
		    ) ||
		    ( l1!=(l1=l&~3U) && l1 && b->ks[l1] &&
			(kc=b->kc[l1])>=minkc && kc<=maxkc &&
			(ks=XkbKeycodeToKeysym(dpy, kc, group, l))
		    ) ||
		    ( b->ks[l1=0] &&
			(kc=b->kc[l1])>=minkc && kc<=maxkc &&
			(ks=XkbKeycodeToKeysym(dpy, kc, group, l))
		    ) ||
		    ( l>3 && b->ks[l1=2] &&
			(kc=b->kc[l1])>=minkc && kc<=maxkc &&
			(ks=XkbKeycodeToKeysym(dpy, kc, group, l))
		    )
		)) {
			b->kc[l]=kc;
			b->mods[l]=m=MODS(l);
			b->ks[l]=ks;
			goto found;
		} else if (l<4) {
			m = MODS(l);
			if (!ks && (ks = b->ks[l&2]?:b->ks[0])) {
				m = MODS(l);
				XkbTranslateKeySym(dpy,&ks,m,buf,1,&n);
				m=0;
			}
#ifdef SLIDERS
		} else if (!ks && (ks = b->ks[l1=l&3])) {
			b->kc[l]=b->kc[l1];
			m = b->mods[l1];
			if (!l1) m|=STATE(KBIT_CTRL);
			b->mods[l]=m;
			// sliders. don't need to be visible, but can
			//if (l<STD_LEVELS) { b->ks[l]=ks; b->txt[l]=b->txt[l1];}
			continue;
#endif
		}
		if (ks) {
			ks1=ks;
			b->mods[l]=m;
			b->kc[l]=kc=XKeysymToKeycode(dpy,ks);
			if (!XkbLookupKeySym(dpy,kc,m,0,&ks1) || ks1!=ks){
				for(l1=0; l1<STD_LEVELS; l1++) if ((m=MODS(l1))!=b->mods[l]) {
					if (XkbLookupKeySym(dpy,kc,m,0,&ks1) && ks1==ks) {
						b->mods[l]=m;
						break;
					}
				}
			}
		}
found:
//		for(l1=0; l1<l; l1++) if (b->ks[l1]==b->ks[l]) {
//			b->mods[l]=b->mods[l1];
//			break;
//		}
		if (ks && l<STD_LEVELS && !(txt = b->txt[l])) {
			for (l1 = 0; l1<l && !txt; l1++) if (ks == b->ks[l1]) txt = b->txt[l1];
#ifndef MINIMAL
			if (!txt) {
			    ksText_(ks,&txt,&l1);
			    if (!l) is_sym = l1;
			    if (
				!strncmp(txt,"XF86Switch_",n=11) ||
				!strncmp(txt,"XF86_",n=5) ||
				!strncmp(txt,"XF86",n=4) ||
				!strncmp(txt,"KP_",n=3)
				) txt+=n;
			}
#endif
			b->txt[l] = txt;
                }
	}

	int w = 0;
	if (b->ks[0]>=0xff80 && b->ks[0]<=0xffb9) {
		// KP_
//		for(l=0;l<LEVELS;l++) b->mods[l]&=~(STATE(KBIT_ALT)|STATE(KBIT_MOD));
		if (b->bg_gc == b->kb->rev_gc) b->bg_gc = b->kb->kp_gc;
		if (b->kb->kp_width) w = b->kb->kp_width;
	} else if ( b->bg_gc == b->kb->rev_gc && (b->modifier || !b->ks[0] || !is_sym))
		b->bg_gc = b->kb->grey_gc;

	if(!b->width) b->width = w?:b->kb->def_width;
	if(!b->height) b->height = b->kb->def_height;
	if(b->width==-1) b->width = 0;
	if(b->height==-1) b->height = 0;

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
	box *bx1 = (box*)b->parent;
	box *vbox = bx1->parent, *bx;
	list *listp, *ip;
	int x1 = b->x + bx1->x;
	int y1 = b->y + bx1->y;
	int x2 = x1 + b->act_width;
	int y2 = y1 + b->act_height;
	int i;
	int n=0;
	button *buf[MAX_SIBLINGS];
	int bord = 1;
	x1 -= bord;
	y1 -= bord;
	x2 += bord;
	y2 += bord;

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
	XFree(keymap);
	keymap = keymap1;
	minkc = minkc1;
	maxkc = maxkc1;
	notkc = minkc1 - 1;
	ks_per_kc = ks_per_kc1;
	return 1;
   }
   XFree(keymap1);
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
	}
	//else
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
	button *b,*b0;
	int l,i;
	KeySym ks,ks1;
	KeyCode kc;
	int is_sym;

	for(listp = vbox->root_kid; listp; listp = listp->next) {
		box_add_box(bx, bx1=_clone_box((box *)listp->data));
		for (ip = ((box *)listp->data)->root_kid; ip; ip = ip->next) {
			b0 = (button *)ip->data;
			memcpy(b=malloc(sizeof(button)),b0,sizeof(button));
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
					if (ksText_(b->ks[l]=ks1,&b->txt[l],&is_sym) || b0->txt[l]!=b->txt[l]) {
#ifdef CACHE_SIZES
						b->txt_size[l]=0;
#endif
#ifdef CACHE_PIX
						if (cache_pix) for (i=0; i<4; i++) b->pix[(l<<2)|i]=0;
#endif
					}
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
		 char *font_name, char *font_name1)
{
  keyboard *kb = NULL;

  list *listp;

  FILE *rcfp;
  char rcbuf[255];		/* buffer for conf file */
  char *tp;                     /* tmp pointer */

  char tmpstr_A[128];
  char tmpstr_C[128];

  box *tmp_box = NULL;
  button *tmp_but = NULL;
  int line_no = 0;
  enum { none, kbddef, rowdef, keydef } context;

  Colormap cmp;

#ifdef USE_XFT
  XRenderColor colortmp;
#endif

#ifndef MINIMAL
  ks2unicode_init();
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

  _simple_GC(kb,&kb->grey_gc,1);
  _simple_GC(kb,&kb->kp_gc,1);

#ifdef USE_XFT
  kb->render_type = xft;
#else
  kb->render_type = oldskool;
#endif
  if (font_name1) {
	_kb_load_font(kb, font_name1);
	kb->xftfont1 = kb->xftfont;
	kb->xftfont = NULL;
	loaded_font = NULL;
  }
  if (font_name) _kb_load_font(kb, font_name);

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
		     &kb->color_rev);

  colortmp.red   = 0x0000;
  colortmp.green = 0x0000;
  colortmp.blue  = 0x0000;
  colortmp.alpha = 0xFFFF;
  XftColorAllocValue(display,
		     DefaultVisual(display, DefaultScreen(display)),
		     DefaultColormap(display,DefaultScreen(display)),
		     &colortmp,
		     &kb->color);

  /* --- end xft bits -------------------------- */

  kb->xftdraw = NULL;
#endif

  /* Defaults */
  kb->theme            = rounded;
  kb->key_delay_repeat = 50;
  kb->key_repeat       = -1;

  kb->total_layouts = 0;


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
		  break;
		case rowdef:

		  break;
		case keydef:
		  button_update(tmp_but);
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
		if ((strcmp(tmpstr_A, "font") == 0) && !loaded_font)
		     _kb_load_font(kb,tmpstr_C );
		else if (strcmp(tmpstr_A, "button_style") == 0)
		  {
		    if (strcmp(tmpstr_C, "square") == 0) {
		      kb->theme = square;
		    } else if (strcmp(tmpstr_C, "plain") == 0)
		      kb->theme = plain;
		    if (kb->theme != rounded)
			XSetLineAttributes(display, kb->bdr_gc, 1, LineSolid, CapButt, JoinMiter);

		  }
		else if (strcmp(tmpstr_A, "col") == 0)
		  _set_color_fg(kb,tmpstr_C,&kb->rev_gc,NULL);
		else if (strcmp(tmpstr_A, "border_col") == 0)
		  _set_color_fg(kb,tmpstr_C,&kb->bdr_gc,NULL);
		else if (strcmp(tmpstr_A, "down_col") == 0)
		  _set_color_fg(kb,tmpstr_C,&kb->gc,NULL);
		else if (!strcmp(tmpstr_A, "gray_col") || !strcmp(tmpstr_A, "grey_col"))
		  _set_color_fg(kb,tmpstr_C,&kb->grey_gc,NULL);
		else if (!strcmp(tmpstr_A, "kp_col"))
		  _set_color_fg(kb,tmpstr_C,&kb->kp_gc,NULL);
		else if (strcmp(tmpstr_A, "width") == 0)
			kb->width=atoi(tmpstr_C);
		else if (strcmp(tmpstr_A, "height") == 0)
			kb->height=atoi(tmpstr_C);
#ifdef SLIDES
		else if (strcmp(tmpstr_A, "slide_margin") == 0)
		     kb->slide_margin = atoi(tmpstr_C);
#endif
		else if (strcmp(tmpstr_A, "repeat_delay") == 0)
		     kb->key_delay_repeat = atoi(tmpstr_C);
		else if (strcmp(tmpstr_A, "repeat_time") == 0)
		     kb->key_repeat = atoi(tmpstr_C);
		else if (strcmp(tmpstr_A, "txt_col") == 0)
		     _set_color_fg(kb,tmpstr_C,&kb->txt_gc,&kb->color);
		else if (strcmp(tmpstr_A, "txt_col_rev") == 0)
		     _set_color_fg(kb,tmpstr_C,&kb->txt_rev_gc,&kb->color_rev);
		else if (!strcmp(tmpstr_A, "def_width"))
		     kb->def_width = atoi(tmpstr_C);
		else if (!strcmp(tmpstr_A, "def_height"))
		     kb->def_height = atoi(tmpstr_C);
		else if (!strcmp(tmpstr_A, "kp_width"))
		     kb->kp_width = atoi(tmpstr_C);
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
		else if (!strcmp(tmpstr_A, "modifier"))
		  tmp_but->modifier = atoi(tmpstr_C);
		else if (!strcmp(tmpstr_A, "lock")) {
		  tmp_but->modifier = atoi(tmpstr_C);
		  tmp_but->flags & STATE(OBIT_LOCK);
		}
#ifdef USE_XPM
		else if (strcmp(tmpstr_A, "img") == 0)
		  { button_set_pixmap(tmp_but, tmpstr_C); }
#endif
		else if (strcmp(tmpstr_A, "bg") == 0)
		  {tmp_but->bg_gc=NULL; _set_color_fg(kb,tmpstr_C,&tmp_but->bg_gc,NULL);}
		else if (strcmp(tmpstr_A, "fg") == 0)
		  {tmp_but->fg_gc=NULL; _set_color_fg(kb,tmpstr_C,&tmp_but->fg_gc,NULL);}
		else if (strcmp(tmpstr_A, "txt_col") == 0)
		  _set_color_fg(kb,tmpstr_C,NULL,&tmp_but->col);
		else if (strcmp(tmpstr_A, "txt_col_rev") == 0)
		  _set_color_fg(kb,tmpstr_C,NULL,&tmp_but->col_rev);
		else if (strcmp(tmpstr_A, "slide_up_ks") == 0)
		  button_set_slide_ks(tmp_but, tmpstr_C, UP);
		else if (strcmp(tmpstr_A, "slide_down_ks") == 0)
		  button_set_slide_ks(tmp_but, tmpstr_C, DOWN);
		else if (strcmp(tmpstr_A, "slide_left_ks") == 0)
		  button_set_slide_ks(tmp_but, tmpstr_C, LEFT);
		else if (strcmp(tmpstr_A, "slide_right_ks") == 0)
		  button_set_slide_ks(tmp_but, tmpstr_C, RIGHT);
		else if (strcmp(tmpstr_A, "vwidth") == 0)
		{
		    tmp_but->flags |= STATE(OBIT_WIDTH_SPEC);
		    tmp_but->vwidth = atoi(tmpstr_C);
		}
		else if (strcmp(tmpstr_A, "width") == 0)
			tmp_but->width = atoi(tmpstr_C);
		else if (strcmp(tmpstr_A, "key_span_width") == 0)
		{
		   tmp_but->flags |= STATE(OBIT_WIDTH_SPEC);
		   tmp_but->key_span_width = atoi(tmpstr_C);
		}

		else if (strcmp(tmpstr_A, "vheight") == 0)
		  tmp_but->vheight = atoi(tmpstr_C);
		else if (strcmp(tmpstr_A, "height") == 0)
		  tmp_but->height = atoi(tmpstr_C);
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

  kb->key_delay_repeat1 = kb->key_delay_repeat;
  kb->key_repeat1 = kb->key_repeat;

  kb->vvbox = kb->vbox = kb->kbd_layouts[kb->group = 0];

  return kb;

}

void cache_preload(keyboard *kb,int layout){
	int i,m;
	list *listp, *ip;
	button *b;
	unsigned int st = kb->state;

	for (listp = kb->kbd_layouts[layout]->root_kid; listp; listp = listp->next) {
		for(ip=((box *)listp->data)->root_kid; ip; ip= ip->next) {
			b = (button *)ip->data;
			for (i=0; i<STD_LEVELS; i++) {
				kb->state = (i&1)|((i&3)?STATE(KBIT_ALT):0)|((i&3)?STATE(KBIT_CTRL):0);
				button_render(b,STATE(OBIT_PRESSED));
				button_render(b,STATE(OBIT_LOCKED));
				button_render(b,0);
			}
		}
	}
	kb->state = st;
}

static char fname[32] = "";

void kb_size(keyboard *kb) {
	long w,h,mw,mh,w1,w2,h1,h2;
	list *listp, *ip;
	button *b;
	int i,j,k;
	box *vbox, *bx;
	static unsigned long long init_cnt = 0;

	// [virtual] kb size based on buttons
	w=0; h=0;
	for(i=0;i<kb->total_layouts;i++) {
		h1=0;
		for (listp = kb->kbd_layouts[i]->root_kid; listp; listp = listp->next) {
			w2=0; h2=0;
			bx=(box *)listp->data;
			bx->x=0; bx->y=h1;
			bx->undef=0;
			for(ip=bx->root_kid; ip; ip= ip->next) {
				b = (button *)ip->data;
				w2+=b->width+b->b_size*2;
				h2=max(h2,b->height+b->b_size*2);
				if (!b->width) bx->undef++;
			}
			bx->width = w2;
			bx->height = h2;
			h1+=h2;
			w=max(w,w2);
		}
		h=max(h,h1);
	}
	if (!kb->width) kb->width=w;
	if (!kb->height) kb->height=h;

	// actual kb size, based on virtual size & screen size & DPI
	if (!kb->vbox->act_height || !kb->vbox->act_width) {
	    w2 = scr_mwidth?ldiv(scr_width*kb->width,scr_mwidth).quot:0;
	    h2 = scr_mheight?ldiv(scr_height*kb->height,scr_mheight).quot:0;
	    unsigned long d1=w2,d2=h2;
	    if (!w2 || !h2) {
		d1=3;
		d2=1;
	    }
	    float d=(w2 && h2)?(w2+0.)/h2:3;
	    if (!kb->vbox->act_height && !kb->vbox->act_width) {
		kb->vbox->act_width=scr_width;
		kb->vbox->act_height=ldiv(min(scr_height,scr_width)*d2,d1).quot;
		if (w2 && w2<kb->vbox->act_width) kb->vbox->act_width=w2;
		if (h2 && h2<kb->vbox->act_height) kb->vbox->act_height=h2;
	    } else if (!kb->vbox->act_height) {
		kb->vbox->act_height=ldiv(min(scr_height,kb->vbox->act_width)*d2,d1).quot;
		if (!h2){
		} else if (!w2) kb->vbox->act_height=h2;
		else kb->vbox->act_height=min(kb->vbox->act_height,ldiv(h2*kb->vbox->act_width,w2).quot);
	    } else if (!kb->vbox->act_width) {
		kb->vbox->act_width=min(scr_width,ldiv(kb->vbox->act_height*d1,d2).quot);
		if (!w2){
		} else if (!h2) kb->vbox->act_width=w2;
		else kb->vbox->act_width=min(kb->vbox->act_width,ldiv(w2*kb->vbox->act_height,h2).quot);
	    }
	}

	w1 = kb->vbox->act_width;
	h1 = kb->vbox->act_height;
	mw = kb->width?:w1;
	mh = kb->height?:h1;

	if (!loaded_font) {
#ifdef USE_XFT
#define FNT "Monospace-%i"
		sprintf(fname,FNT,10);
		_kb_load_font(kb, fname);
		i = ldiv(w1,_button_get_txt_size(kb,"ABCabc123+")).quot;
		if (i!=10) {
			sprintf(fname,FNT,i);
			free(loaded_font);
			loaded_font = NULL;
			_kb_load_font(kb, fname);
		}
#else
		_kb_load_font(kb, fname);
#define fname "fixed"
#endif
		free(loaded_font);
		loaded_font = NULL;
	}
#ifdef USE_XFT
	if (!kb->xftfont1) kb->xftfont1 = kb->xftfont;
#endif

	int cy = 0;
	int max_single_char_width = 0;
	int max_single_char_height = 0;
	int max_width = 0; /* required for sizing code */

	for(i=0;i<kb->total_layouts;i++) {
		kb->vbox = kb->kbd_layouts[i];
		for (listp = kb->vbox->root_kid; listp; listp = listp->next) {
			bx=(box *)listp->data;
			bx->x=ldiv(bx->x*w1,mw).quot;
			bx->y=ldiv(bx->y*h1,mh).quot;
			for(ip=((box *)listp->data)->root_kid; ip; ip= ip->next) {
				b = (button *)ip->data;
				if (!b->width && bx->undef) bx->width += (b->width = div(w - bx->width, bx->undef--).quot);
				if (init_cnt) {
#ifdef CACHE_SIZES
				    memset(&b->txt_size,0,sizeof(b->txt_size));
#endif
#ifdef CACHE_PIX
				    if (cache_pix) for (k=0; k<(STD_LEVELS<<2); k++) {
					Pixmap pix=b->pix[k];
					if (pix) {
						for (j=k; j<(STD_LEVELS<<2); j++) if (b->pix[j] == pix) b->pix[j]=0;
						XFreePixmap(b->kb->display, pix);
					}
				    }
#endif
				}
				button_calc_vwidth(b);
				button_calc_vheight(b);
				if (!(b->flags & STATE(OBIT_WIDTH_SPEC))) {
					if ( ( DEFAULT_TXT(b) == NULL || strlen1utf8(DEFAULT_TXT(b)))
						&& (SHIFT_TXT(b) == NULL || strlen1utf8(SHIFT_TXT(b)))
						&& (MOD_TXT(b) == NULL || strlen1utf8(MOD_TXT(b)))
						&& b->pixmap == NULL) {
							if (b->vwidth > max_single_char_width)
								max_single_char_width = b->vwidth;
					} 
				}
				if (b->vheight > max_single_char_height) max_single_char_height = b->vheight;
			}
		}
	}
	kb->vbox = kb->kbd_layouts[kb->group = 0];

	if (cache_pix) {
	    if (kb->backing) XFreePixmap(kb->display, kb->backing);

	    kb->backing = XCreatePixmap(kb->display, kb->win,
		kb->vbox->act_width, kb->vbox->act_height,
		DefaultDepth(kb->display, DefaultScreen(kb->display)) );
	// runtime disabling cache -> direct rendering
	} else kb->backing = kb->win;

#ifdef USE_XFT
	if (kb->xftdraw != NULL) XftDrawDestroy(kb->xftdraw);


	kb->xftdraw = XftDrawCreate(kb->display, (Drawable) kb->backing,
			       DefaultVisual(kb->display,
					     DefaultScreen(kb->display)),
			       DefaultColormap(kb->display,
					       DefaultScreen(kb->display)));
#endif


	/* Set all single char widths to the max one, figure out minimum sizes */
//	if (1 || max_single_char_width || max_single_char_height) {
		max_single_char_height += 2;
		for(i=0;i<kb->total_layouts;i++) {
			box *vbox = kb->kbd_layouts[i];
			if (!cache_pix) {
				vbox->vx = vbox->x;
				vbox->vy = vbox->y;
			}
			for (listp = vbox->root_kid; listp; listp = listp->next) {
				int tmp_width = 0;
				int tmp_height = 0;
				int max_height = 0;
				bx = (box *)listp->data;
				for(ip=bx->root_kid; ip; ip= ip->next) {
					b = (button *)ip->data;
					if (!(b->flags & STATE(OBIT_WIDTH_SPEC))) {
						 if ((DEFAULT_TXT(b) == NULL || (strlen(DEFAULT_TXT(b)) == 1))
						    && (SHIFT_TXT(b) == NULL || (strlen(SHIFT_TXT(b)) == 1))
						    && (MOD_TXT(b) == NULL || (strlen(MOD_TXT(b)) == 1))
						    && b->pixmap == NULL ) {
							b->vwidth = max_single_char_width;
							b->vheight = max_single_char_height;
							if (b->key_span_width) b->vwidth = b->key_span_width * max_single_char_width;
						}
					}

					tmp_width += b->vwidth + b->b_size*2;
					tmp_height = b->vheight + b->b_size*2;
					if (tmp_height >= max_height) max_height = tmp_height;
				}
				if (tmp_width > max_width) max_width = tmp_width;
				if (tmp_height >= max_height) max_height = tmp_height;
				bx->min_width  = tmp_width;
				bx->min_height = max_height;
				vbox->min_height += max_height; // +1;
			}
			if ((i > 0) && vbox->min_height > kb->kbd_layouts[0]->min_height)
				kb->kbd_layouts[0]->min_height = vbox->min_height;
			vbox->min_width = max_width;
		}
		for(i=0;i<kb->total_layouts;i++) {
		    box *vbox = kb->kbd_layouts[i];
		    kb->vbox = vbox;
		    for (listp = vbox->root_kid; listp; listp = listp->next) {
			int cx = 0;
			//int total = 0;
			int y_pad = 0;
			bx = (box *)listp->data;
			bx->y = cy;
			bx->x = 0;
			y_pad =  ldiv((unsigned long)bx->min_height * vbox->act_height,vbox->min_height).quot;
			for(ip=bx->root_kid; ip; ip= ip->next) {
				int but_total_width;
				b = (button *)ip->data;
				b->x = cx; /*remember relative to holding box ! */
				but_total_width = b->vwidth+(2*b->b_size);
				b->x_pad = ldiv((unsigned long) but_total_width * vbox->act_width,bx->min_width).quot;
				b->x_pad -= but_total_width;
				b->act_width=bx->width?ldiv((unsigned long)b->width*vbox->act_width,bx->width).quot:b->vwidth + b->x_pad + b->b_size*2;
				cx += b->act_width;
				b->y = 0;
				b->y_pad = y_pad - b->vheight - b->b_size*2;
				b->act_height = y_pad;
//				b->act_height = bx->height?ldiv(b->height*vbox->act_height,bx->height).quot:y_pad;
				/*  hack for using all screen space */
				if (listp->next == NULL) b->act_height--;
				b->vx = button_get_abs_x(b) - vbox->x + vbox->vx;
				b->vy = button_get_abs_y(b) - vbox->y + vbox->vy;
			}
			/*  another hack for using up all space */
			b->x_pad += (vbox->act_width-cx) -1 ;
			b->act_width += (vbox->act_width-cx) -1;
			cy += y_pad ; //+ 1;
			bx->act_height = y_pad;
			bx->act_width = vbox->act_width;
		    }
		}
/*
	} else {
		// new code?
		int x,y;
		y=0;
		for (listp = kb->vbox->root_kid; listp; listp = listp->next) {
			x=0;
			bx = (box *)listp->data;
			for(ip=bx->root_kid; ip; ip= ip->next) {
				b = (button *)ip->data;
				b->x=x;
				b->y=y;
				b->act_width = b->width*w1/mw;
				b->act_height = b->height*h1/mh;
				x+=b->act_width;
				fprintf(stderr,"%i ",x);
			}
			y+=bx->act_height;
		}
	}
*/
	kb->vbox = kb->kbd_layouts[kb->group = 0];

	/* TODO: copy all temp vboxs  */


#ifdef SIBLINGS
    kb_update(kb);
#endif

#if 0
    int N;
    for (N=0; N<1024; N++)
#endif
    if (cache_pix>1) for(i=0;i<kb->total_layouts;i++) cache_preload(kb,i);
    init_cnt++;
}

void
kb_switch_layout(keyboard *kb, int kbd_layout_num, int shift)
{
  box *b;
  box *b0 = kb->vvbox;

  for(; kbd_layout_num >= kb->total_layouts; kb->total_layouts++) {
	kb->kbd_layouts[kb->total_layouts] = clone_box(kb->display,kb->kbd_layouts[0],kb->total_layouts);
	if (cache_pix>1) cache_preload(kb,kb->total_layouts);
  }

  b = kb->kbd_layouts[kb->group = kbd_layout_num];
  kb->vbox = b;
  if (!shift) kb->vvbox = b;
  if (b0 == kb->vvbox) return;

//  kb_size(kb);
  kb_render(kb);
  kb_paint(kb);
}

void kb_render(keyboard *kb)
{
	list *listp, *ip;
	XFillRectangle(kb->display, kb->backing,
		kb->filled=kb->rev_gc, kb->vvbox->vx, kb->vvbox->vy,
		kb->vvbox->act_width, kb->vvbox->act_height);
	for (listp = kb->vvbox->root_kid; listp; listp = listp->next) {
		for(ip=((box *)listp->data)->root_kid; ip; ip= ip->next) {
			button *b = (button *)ip->data;
			button_render(b, (b->modifier & kb->state_locked)?STATE(OBIT_LOCKED):(b->modifier & kb->state)?STATE(OBIT_PRESSED):0);
		}
	}
	kb->filled=NULL;
}

void kb_paint(keyboard *kb)
{
  if (cache_pix)
  XCopyArea(kb->display, kb->backing, kb->win, kb->gc,
	    0, 0, kb->vbox->act_width, kb->vbox->act_height,
	    kb->vbox->x, kb->vbox->y);
}

static inline void bdraw(button *b, int flags){
	keyboard *kb=b->kb;
	if (kb->vbox!=kb->vvbox){
		if (b->layout_switch==-1) return;
		flags|=STATE(OBIT_PRESSED);
	}
	if (!button_render(b, flags|STATE(OBIT_DIRECT)))
	button_paint(b);
}

void _release(button *b){
#ifdef MULTITOUCH
	if (b->flags & STATE(OBIT_PRESSED))
		kb_process_keypress(b,0,STATE(OBIT_UGLY));
	else
#endif
	bdraw(b,0);
}

void _press(button *b, unsigned int flags){
	flags |= STATE(OBIT_PRESSED);
#ifdef MULTITOUCH
	if (b->modifier & KB_STATE_KNOWN && !(b->flags & STATE(OBIT_LOCK)))
		kb_process_keypress(b,0,flags);
	else
#endif
	bdraw(b,flags);
}

#if MAX_TOUCH == 16
#define TOUCH_INC(x) (x=(x+1)&15)
#define TOUCH_DEC(x) (x=(x+15)&15)
#else
#define TOUCH_INC(x) (x=(x+1)%MAX_TOUCH)
#define TOUCH_DEC(x) (x=(x+(MAX_TOUCH-1))%MAX_TOUCH)
#endif

button *kb_handle_events(keyboard *kb, int type, int x, int y, uint32_t ptr, int dev, Time time)
{
	button *b, *b1;
	int i,j;
	int n;
	Time T;

	static uint32_t touchid[MAX_TOUCH];
	static int devid[MAX_TOUCH];
	static Time times[MAX_TOUCH];
	static button *but[MAX_TOUCH];
#ifdef SIBLINGS
	static button *sib[MAX_TOUCH][MAX_SIBLINGS];
	static short nsib[MAX_TOUCH] = {};
#endif

#ifdef DO_CNT
	int cnt=0;
#define IF_CNT(B) if (!cnt) for (i=P; i!=N; TOUCH_INC(i)) if (but[i] == B && devid[i] == dev) cnt++; if (cnt<2)
#else
#define IF_CNT(B)
#endif

#ifdef MULTITOUCH
	static unsigned int N=0;
	static unsigned int P=0;
	int t=-1;

	// find touch
	for (i=P; i!=N; TOUCH_INC(i)) {
		if (touchid[i] == ptr && devid[i] == dev) {
			// duplicate (I got BEGIN!)
//			if (time==times[i] && x==X[i] && y==Y[i]) return but[i];
			if (time<=times[i] && type!=2) return but[i];
			t=i;
			break;
		}
	}
	if (type && t<0) {
//		fprintf(stderr,"untracked touch on state %i\n",type);
		return NULL;
	}

#else
	const int t=0;
#endif
	b = kb_find_button(kb,x,y);

	if (!type) { // BEGIN/press
		if (!b) return NULL;
#ifdef MULTITOUCH
		t=N;
		TOUCH_INC(N);
		if (N==P) TOUCH_INC(P);
#endif
#ifdef SIBLINGS
		n = nsib[t];
		for(i=0;i<n;i++) if ((b1=sib[t][i])!=but[t]) _release(b1);
		nsib[t] = -1;
#endif
		if ((b1=but[t]) && b1!=b) {
			IF_CNT(b1) {
				_release(b1);
				b1=NULL;
			}
		}
		but[t] = b;
		times[t] = time;
		touchid[t] = ptr;
		devid[t] = dev;
		if (b1!=b) _press(b,0);
		return b;
	}

	b1 = but[t];
#ifndef SIBLINGS
	if (b != b1 && b1) goto drop;
#else
	/*
		intersection of sets of sibling buttons
		previous button or intersection with new button.
		if set size is 1 - this is result of touch.
		else must do other selections.
	*/
	n=nsib[t];
	if (!b) {
	} else if (!b1) { // NULL -> button: new siblings base list
		// probably never, but keep case visible
		for(i=0;i<n;i++) _release(sib[t][i]);
		nsib[t]=-1;
		_press(b,STATE(OBIT_UGLY));
	} else if (b1==b) { // first/main button - reset motions
		for(i=0;i<n;i++) if (sib[t][i]!=b) _release(sib[t][i]);
		//nsib[t]=1; sib[t][0]=b;
		nsib[t]=-1;
	} else if (b1->flags & STATE(OBIT_PRESSED)) { // slide from pressed
		b=b1;
	} else if (b->flags & STATE(OBIT_PRESSED)) { // pressed somewere
		b=NULL;
	} else { // button -> button, invariant
		int ns, ns1 = b->nsiblings;
		button **s1 = (button **)b->siblings;
		if (n<0) {
			button **s = (button **)b1->siblings;
			ns = b1->nsiblings;
			n = 0;
			for(i=0; i<ns; i++) {
				b1=s[i];
				for(j=0; j<ns1; j++) {
					if (b1==s1[j]) {
						_press(sib[t][n++]=b1,STATE(OBIT_UGLY));
						break;
					}
				}
			}
		} else {
			ns = n;
			n = 0;
			for(i=0; i<ns; i++) {
				b1=sib[t][i];
				for(j=0; j<ns1; j++) {
					if (b1==s1[j]) {
						sib[t][n++]=b1;
						b1=NULL;
						break;
					}
				}
				if (b1) _release(b1);
			}
		}
		nsib[t]=n;
		if (n==0) { // no siblings, drop touch
			goto drop;
		} else if (n==1) { // 1 intersection: found button
			but[t] = b = sib[t][0];
		} else { // multiple, need to calculate or touch more
#if 0
			if (type==2) goto drop;
#else
			// check set of 2 buttons (first & last)
			button *s2[2] = { but[t], b };
			int n1=0;
			for(j=0; j<2; j++) {
				b1=s2[j];
				for(i=0; i<n; i++) if (sib[t][i]==b1) {
					s2[n1++]=b1;
					break;
				}
			}
			if (n1==1) {
				but[t] = b = s2[0];
				// in this place we can select single button, but no preview is wrong
#if 0
				// 2do? keep all set, but highlite or "press" (release)
				else if (type==1) _press(b,STATE(OBIT_UGLY)|STATE(OBIT_SELECT));
				if (type==2) nsib[t]=1;
#endif
			} else b = n1 ? but[t] : NULL;
#endif
		}
	}
#endif // SIBLINGS

	if (type!=2){ // motion/to be continued
		times[t] = time;
		return b;
	}

	// the END/release
	if (!b) goto drop;
#ifdef SIBLINGS
	// on any logic, don't press without preview
	if (nsib[t]>1) goto drop;
#endif
#ifdef SLIDES
	kb_set_slide(b, x, y );
#endif
	IF_CNT(b) kb_process_keypress(b,0,0);
	if (b->layout_switch != -1) {
		b->flags &= ~(STATE(OBIT_PRESSED)|STATE(OBIT_UGLY));
#ifdef SIBLINGS
		n = nsib[t];
		for(i=0;i<n;i++) sib[t][i]->flags &= ~(STATE(OBIT_PRESSED)|STATE(OBIT_UGLY));
		N=P=0;
#endif
		return NULL;
	}
	if (b->modifier) but[t] = NULL;
drop:
#ifdef DO_CNT
	if (b!=but[t]) {
		b=but[t];
		cnt=0;
	}
	if (b) {
#else
	if (b=but[t]) {
#endif
		but[t] = NULL;
		IF_CNT(b) _release(b);
#ifdef SLIDES
		b->slide = 0;
#endif
	}
#ifdef MULTITOUCH
	TOUCH_DEC(N);
#ifdef SIBLINGS
	n = nsib[t];
	for(i=0;i<n;i++) if ((b1=sib[t][i])!=b) _release(b1);
	nsib[t]=-1;
#endif
	if (t!=N) {
		but[t] = but[N];
		touchid[t] = touchid[N];
		devid[t] = devid[N];
		times[t] = times[N];
#ifdef SIBLINGS
		n = nsib[t] = nsib[N];
		for(i=0;i<n;i++) sib[t][i]=sib[N][i];
#endif
	}
#endif
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
    int layout = b->layout_switch;
    int keypress = 1;
    unsigned int st,st0;
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
		if (b->flags & STATE(OBIT_LOCK)) lock |= mod;
	} else if (no_lock) {
		state ^= mod;
	} else {
		lock ^= mod;
	}
	DBG("got a modifier key - %i \n", state);
    } else {
	state_used |= st0 = state|lock;
	state = lock;
	DBG("kbd is shifted, unshifting - %i \n", state);
    }

    st = (state|lock) & KB_STATE_KNOWN;

    if (keypress) {
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

   if (layout!=-1) {
    if (layout>-1) {
#ifndef MINIMAL
	if (Xkb_sync) {
		XSync(dpy,False);
		XkbLockGroup(dpy, XkbUseCoreKbd, b->layout_switch);
		XSync(dpy,True);
	}
#endif
    } else {
        XkbStateRec s;
	XkbGetState(dpy,XkbUseCoreKbd,&s);
	layout = s.group;
    }
    // shift-layout: trans-layout mode (silent)
    st0 &= STATE(KBIT_SHIFT);
    kb_switch_layout(kb,layout,st0);
    if (st0)
	bdraw(b,0);
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
	if ((mods0 ^ mods) & BIT_MVL(b->flags,OBIT_OBEYCAPS,KBIT_CAPS)) mods ^= STATE(KBIT_CAPS)|STATE(KBIT_SHIFT);
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
		if (y >= i+bx->act_height) continue;
		for(ip=bx->root_kid; ip; ip=ip->next) {
			tmp_but = (button *)ip->data;
			i = tmp_but->x+bx->x;
			if (x<i) break;
			if (x>=i+tmp_but->act_width) continue;
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
      box = (box *)listp->data;
      box_destroy(box) -- this will destroy the buttons

    }
  */

  free(kb);
}
