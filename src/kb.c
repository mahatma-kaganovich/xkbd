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
#define ksText_(ks,x,y) ""
#endif

#ifdef DEBUG
#define DBG(txt, args... ) fprintf(stderr, "DEBUG" txt "\n", ##args )
#else
#define DBG(txt, args... ) /* nothing */
#endif

#define DEFAULT_FONT "Monospace-%i|-%i|sans-%i|fixed-%i|fixed"

KeySym *keymap = NULL;
int minkc = 0;
int maxkc = 0;
int notkc = 0;
int ks_per_kc = 0;

char *font = NULL;
char *font1 = NULL;
char *loaded_font = NULL;
char *loaded_font1 = NULL;

static void kb_process_keypress(button *b, int repeat, unsigned int flags);
static int kb_switch_layout(keyboard *kb, int kbd_layout_num, int shift);

static int load_a_single_font(keyboard *kb, char *fontname, FNTYPE *f) {
#ifdef USE_XFT
  if (*f) XftFontClose(kb->display, *f);
  return ((*f = XftFontOpenName(kb->display, kb->screen, fontname)) != NULL);
#else
  if (*f) XUnloadFont((*f)->fid);
  if ((*f = XLoadQueryFont(kb->display, fontname)) == NULL) return 0;
  XSetFont(kb->display, kb->gc, (*f)->fid);
  return True;
#endif
}

static void load_font(keyboard *kb, char **loaded, char *fnt, FNTYPE *f){
	char *fnames0, *fnames, *fname, *fname1;
	int i;

	if (*loaded) {
		if (!strcmp(*loaded,fnt)) return;
		free(*loaded);
	}
	fnames0 = fnames = strdup(fnt);
	*loaded = fname1 = malloc(strlen(fnt)+10);
	while((fname = strsep(&fnames, "|"))) {
		sprintf(fname1,fname,10);
		if (!load_a_single_font(kb,fname1,f)) continue;
		// font found
		if (strcmp(fname1,fname)) {
			// font is variable print mask. scale
			i = ldiv(kb->vbox->act_width, _button_get_txt_size(kb,"ABCabc123+")).quot;
			if (i && i!=10 && i<1000) {
				sprintf(fname1,fname,i);
				if (!load_a_single_font(kb,fname1,f)) continue;
			}
		}
		free(fnames0);
		return;
	}
	fprintf(stderr, "xkbd: unable to find suitable font in '%s'\n", fnt);
	exit(1);
}

static void button_update(button *b) {
	int l,l1;
	keyboard *kb = b->kb;
	int group = kb->total_layouts-1;
	Display *dpy = kb->display;
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

	if (is_sym && kb->txt_sym_gc) {
		b->txt_gc = kb->txt_sym_gc;
#ifdef USE_XFT
		b->col = kb->color_sym;
#endif
	}

	int w = 0;
	if (b->ks[0]>=0xff80 && b->ks[0]<=0xffb9) {
		// KP_
//		for(l=0;l<LEVELS;l++) b->mods[l]&=~(STATE(KBIT_ALT)|STATE(KBIT_MOD));
		if (b->bg_gc == kb->rev_gc) b->bg_gc = kb->kp_gc;
		if (kb->kp_width) w = kb->kp_width;
	} else if ( b->bg_gc == b->kb->rev_gc && (b->modifier || !b->ks[0] || !is_sym))
		b->bg_gc = kb->grey_gc;

	if(!b->width) b->width = w?:kb->def_width;
	if(!b->height) b->height = kb->def_height;
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
static void kb_find_button_siblings(button *b) {
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

static void kb_update(keyboard *kb){
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

static unsigned int _set_state(unsigned int *s, unsigned int new)
{
	unsigned int old = *s & KB_STATE_KNOWN;
	new &= KB_STATE_KNOWN;
	*s = (*s & ~KB_STATE_KNOWN)|new;
	return old^new;
}


unsigned int kb_sync_state(keyboard *kb, unsigned int mods, unsigned int locked_mods, int group)
{
	unsigned int ch=0;
	int i=0;

//	locked_mods &= STATE(KBIT_CAPS) | ~KB_STATE_KNOWN;

	if (((mods|locked_mods) ^ ((kb->state|kb->state_locked)) && KB_STATE_KNOWN))
		ch|=_set_state(&kb->state, mods)|_set_state(&kb->state_locked, locked_mods);
	if (group!=kb->group){
		kb_switch_layout(kb,group,0);
		ch=0;
	}
//	XkbGetIndicatorState(kb->display,XkbUseCoreKbd,&i);
	return ch;
}


#ifdef USE_XFT
#define _set_color_fg(kb,c,gc,xc)  __set_color_fg(kb,c,gc,xc)
static void __set_color_fg(keyboard *kb, char *txt,GC *gc, XftColor *xc){
	XRenderColor colortmp;
#else
#define _set_color_fg(kb,c,gc,xc)  __set_color_fg(kb,c,gc)
static void __set_color_fg(keyboard *kb, char *txt ,GC *gc){
#endif
	XColor col = {};
	Display *dpy = kb->display;
	const char delim[] = ",:";
	char *txt1;
	XColor exact;
	int r = 0;

	txt1 = strsep(&txt,"=|");
	if (txt) r=XAllocNamedColor(dpy,  kb->colormap, txt1, &col, &exact);
	else txt = txt1;
	if (!r) {
		txt1=strsep(&txt, delim);
		if (txt) {
			col.red =atoi(txt1) * 257; // *65535/255
			txt1 = strsep(&txt, delim);
			if (txt) {
				col.green = atoi(txt1) * 257;
				txt1 = strsep(&txt, delim);
				col.blue = atoi(txt1) * 257;
				col.flags = DoRed | DoGreen | DoBlue;
				r=XAllocColor(dpy, kb->colormap, &col);
			}
		}
	}
	if (!r && !XAllocNamedColor(dpy,  kb->colormap, txt, &col, &exact)) {
		perror("color allocation failed\n"); exit(1);
	}

#ifdef USE_XFT
	if (xc) {
		colortmp.red   = col.red;
		colortmp.green = col.green;
		colortmp.blue  = col.blue;
		colortmp.alpha = 0xFFFF;
		XftColorAllocValue(dpy,
			kb->visual,
			kb->colormap,
			&colortmp, xc);
	}
	//else
#endif
	{
		if (gc && !*gc) *gc = _createGC(kb,0);
		XSetForeground(dpy, *gc, col.pixel );
	}
}

static box *_clone_box(box *vbox){
	box *b = malloc(sizeof(box));
	memcpy(b,vbox,sizeof(box));
	b->root_kid = b->tail_kid =NULL;
	return b;
}

static box *clone_box(Display *dpy, box *vbox, int group){
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
					b->ks[l]=ks1;
					if (ksText_(ks1,&b->txt[l],&is_sym) || b0->txt[l]!=b->txt[l]) {
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

keyboard* kb_new(Window win, Display *display, int screen, int kb_x, int kb_y,
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

#ifdef USE_XFT
  XRenderColor colortmp;
#endif

#ifndef MINIMAL
  ks2unicode_init();
#endif

  kb = calloc(1,sizeof(keyboard));
  kb->win = win;
  kb->display = display;
  kb->screen = screen;
  kb->visual = DefaultVisual(display, screen);
  kb->colormap = DefaultColormap(display, screen);

  /* create lots and lots of gc's */
  kb->gc=_createGC(kb,0);
  kb->rev_gc=_createGC(kb,1);
  kb->txt_gc=_createGC(kb,0);
  kb->txt_rev_gc=_createGC(kb,1);
  kb->bdr_gc=_createGC(kb,0);

  kb->grey_gc=_createGC(kb,1);
  kb->kp_gc=_createGC(kb,1);

#ifdef USE_XFT

  /* -- xft bits -------------------------------------------- */

  colortmp.red   = 0xFFFF;
  colortmp.green = 0xFFFF;
  colortmp.blue  = 0xFFFF;
  colortmp.alpha = 0xFFFF;
  XftColorAllocValue(display,
		     kb->visual,
		     kb->colormap,
		     &colortmp,
		     &kb->color_rev);

  colortmp.red   = 0x0000;
  colortmp.green = 0x0000;
  colortmp.blue  = 0x0000;
  colortmp.alpha = 0xFFFF;
  XftColorAllocValue(display,
		     kb->visual,
		     kb->colormap,
		     &colortmp,
		     &kb->color);

  /* --- end xft bits -------------------------- */

  kb->xftdraw = NULL;
#endif

  /* Defaults */
  kb->theme            = rounded;
  kb->key_delay_repeat = 50;
  kb->key_repeat       = -1;

  kb->line_width = 1;

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
	      case kbddef:
		if ((strcmp(tmpstr_A, "font") == 0))
			font = strdup(tmpstr_C);
		if ((strcmp(tmpstr_A, "font1") == 0))
			font1 = strdup(tmpstr_C);
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
		else if (!strcmp(tmpstr_A, "gray_col") || !strcmp(tmpstr_A, "grey_col"))
		  _set_color_fg(kb,tmpstr_C,&kb->grey_gc,NULL);
		else if (!strcmp(tmpstr_A, "kp_col"))
		  _set_color_fg(kb,tmpstr_C,&kb->kp_gc,NULL);
		else if (strcmp(tmpstr_A, "border_width") == 0)
			kb->line_width=atoi(tmpstr_C);
		else if (strcmp(tmpstr_A, "button_padding") == 0)
			kb->pad=atoi(tmpstr_C);
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
		else if (strcmp(tmpstr_A, "sym_col") == 0)
		     _set_color_fg(kb,tmpstr_C,&kb->txt_sym_gc,&kb->color_sym);
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
		  DEFAULT_TXT(tmp_but) = strdup(tmpstr_C);
		else if (strcmp(tmpstr_A, "shift") == 0)
		  SHIFT_TXT(tmp_but) = strdup(tmpstr_C);
		else if (strcmp(tmpstr_A, "switch") == 0) {
		  // -1 is default, but setting in config -> -2 & switch KeySym
		  // to other keysym - set -2 & concrete default_ks
		  if((tmp_but->layout_switch = atoi(tmpstr_C))==-1) {
		    button_set_txt_ks(tmp_but, "ISO_Next_Group");
		    tmp_but->layout_switch = -2;
		  }
		} else if (strcmp(tmpstr_A, "mod") == 0)
		  MOD_TXT(tmp_but) = strdup(tmpstr_C);
		else if (strcmp(tmpstr_A, "shift_mod") == 0)
		  SHIFT_MOD_TXT(tmp_but) = strdup(tmpstr_C);
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
		  _set_color_fg(kb,tmpstr_C,&tmp_but->txt_gc,&tmp_but->col);
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

    if (font_name) {
	if (font) free(font);
	font = strdup(font_name);
    } else if (!font) font = strdup(DEFAULT_FONT);

    if (font_name1) {
	if (font1) free(font1);
	font1 = strdup(font_name1);
    }

    switch  (kb->theme) {
	case square: XSetLineAttributes(display, kb->bdr_gc, kb->line_width, LineSolid, CapButt, JoinMiter); break;
	case rounded: XSetLineAttributes(display, kb->bdr_gc, kb->line_width, LineSolid, CapRound, JoinRound); break;
	case plain: kb->line_width = 0; break;
    }

  kb->key_delay_repeat1 = kb->key_delay_repeat;
  kb->key_repeat1 = kb->key_repeat;

  kb->vvbox = kb->vbox = kb->kbd_layouts[kb->group = 0];

  return kb;

}

static void cache_preload(keyboard *kb,int layout){
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

char fname[32] = "";

void kb_size(keyboard *kb) {
	long w,h,mw,mh,w1,w2,h1,h2;
	list *listp, *ip;
	button *b;
	int i,j,k;
	box *vbox, *bx;
	static unsigned long long init_cnt = 0;
	const int hack = 1; /* hack for using all screen space */

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
				w2+=b->width+(b->b_size<<1);
				h2=_max(h2,b->height+(b->b_size<<1));
				if (!b->width) bx->undef++;
			}
			bx->width = w2;
			bx->height = h2;
			h1+=h2;
			w=_max(w,w2);
		}
		h=_max(h,h1);
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
		kb->vbox->act_height=ldiv(_min(scr_height,scr_width)*d2,d1).quot;
		if (w2 && w2<kb->vbox->act_width) kb->vbox->act_width=w2;
		if (h2 && h2<kb->vbox->act_height) kb->vbox->act_height=h2;
	    } else if (!kb->vbox->act_height) {
		kb->vbox->act_height=ldiv(_min(scr_height,kb->vbox->act_width)*d2,d1).quot;
		if (!h2){
		} else if (!w2) kb->vbox->act_height=h2;
		else kb->vbox->act_height=_min(kb->vbox->act_height,ldiv(h2*kb->vbox->act_width,w2).quot);
	    } else if (!kb->vbox->act_width) {
		kb->vbox->act_width=_min(scr_width,ldiv(kb->vbox->act_height*d1,d2).quot);
		if (!w2){
		} else if (!h2) kb->vbox->act_width=w2;
		else kb->vbox->act_width=_min(kb->vbox->act_width,ldiv(w2*kb->vbox->act_height,h2).quot);
	    }
	}

	w1 = kb->vbox->act_width;
	h1 = kb->vbox->act_height;
	mw = kb->width?:w1;
	mh = kb->height?:h1;

	if (kb->font1 == kb->font) kb->font1 = NULL;
	if (font1 && !strcmp(font, font1)) font1 = NULL;
	load_font(kb, &loaded_font, font, &kb->font);
	if (font1) load_font(kb, &loaded_font1, font1, &kb->font1);
	else kb->font1 = kb->font;

#ifdef USE_XFT
	kb->vheight = _max(kb->font->height,kb->font->ascent+kb->font->descent);
	kb->vheight1 = _max(kb->font1->height,kb->font1->ascent+kb->font1->descent);
#else
	kb->vheight = b->kb->font->ascent + b->kb->font->descent;
	kb->vheight1 = b->kb->font1->ascent + b->kb->font1->descent;
#endif


	int max_single_char_width = 0;
	int max_single_char_height = kb->vheight1;
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
				if (b->vheight) continue;
				b->vheight = kb->vheight;
				if (
					!(b->width && b->height && kb->vheight == kb->vheight1) && // opt
					!(b->flags & STATE(OBIT_WIDTH_SPEC))) {
					if ( ( DEFAULT_TXT(b) == NULL || strlen1utf8(DEFAULT_TXT(b)))
						&& (SHIFT_TXT(b) == NULL || strlen1utf8(SHIFT_TXT(b)))
						&& (MOD_TXT(b) == NULL || strlen1utf8(MOD_TXT(b)))
						&& !b->pixmap) {
							b->vheight = kb->vheight1;
							if (b->vwidth > max_single_char_width)
								max_single_char_width = b->vwidth;
					}
				}
				if (b->vheight > max_single_char_height) max_single_char_height = b->vheight;
			}
		}
	}
	kb->vbox = kb->kbd_layouts[kb->group = 0];

	if (cache_pix && cache_pix!=3) {
	    if (kb->backing) XFreePixmap(kb->display, kb->backing);

	    kb->backing = XCreatePixmap(kb->display, kb->win,
		kb->vbox->act_width, kb->vbox->act_height,
		DefaultDepth(kb->display, kb->screen) );
	// runtime disabling cache -> direct rendering
	} else kb->backing = kb->win;

#ifdef USE_XFT
	if (kb->xftdraw != NULL) XftDrawDestroy(kb->xftdraw);


	kb->xftdraw = XftDrawCreate(kb->display, (Drawable) kb->backing,
					    kb->visual, kb->colormap);
#endif


	/* Set all single char widths to the max one, figure out minimum sizes */
//	if (1 || max_single_char_width || max_single_char_height) {
//		max_single_char_height += 2;
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

					if (
						!(b->width && b->height) &&
						!(b->flags & STATE(OBIT_WIDTH_SPEC))) {
						 if ((DEFAULT_TXT(b) == NULL || (strlen(DEFAULT_TXT(b)) == 1))
						    && (SHIFT_TXT(b) == NULL || (strlen(SHIFT_TXT(b)) == 1))
						    && (MOD_TXT(b) == NULL || (strlen(MOD_TXT(b)) == 1))
						    && !b->pixmap) {
							if (!b->height) b->vheight = max_single_char_height;
							if (!b->width) {
								b->vwidth = max_single_char_width;
								if (b->key_span_width) b->vwidth = b->key_span_width * max_single_char_width;
							}
						}
					}
  
					tmp_width += b->vwidth + (b->b_size<<1);
					tmp_height = b->vheight + (b->b_size<<1);
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
		    int cy = 0;
		    int fx = 0;
		    box *vbox = kb->kbd_layouts[i];
		    kb->vbox = vbox;
		    for (listp = vbox->root_kid; listp; listp = listp->next) {
			int cx = 0;
			bx = (box *)listp->data;
			bx->y = cy;
			//bx->x = 0;
			bx->act_width = vbox->act_width;
			bx->act_height = _max(bx->min_height,
			    (!listp->next && vbox->act_height>=scr_height)?
				vbox->act_height - hack - cy:
			    (vbox->height && bx->height)?
				ldiv((unsigned long)bx->height * vbox->act_height,vbox->height).quot:
				ldiv((unsigned long)bx->min_height * vbox->act_height,vbox->min_height).quot);
			cy += bx->act_height;
			for(ip=bx->root_kid; ip; ip= ip->next) {
				b = (button *)ip->data;
				b->x = cx; /*remember relative to holding box ! */
				//b->y = 0;
				b->act_width = _max(b->vwidth + (b->b_size<<1),
				    (!ip->next && vbox->act_width>=scr_width)?
					bx->act_width - hack - cx:
				    (b->width && bx->width)?
					ldiv((unsigned long)b->width*vbox->act_width,bx->width).quot:
					ldiv((unsigned long)b->vwidth*vbox->act_width,bx->min_width).quot);
				b->act_height = bx->act_height;
//				b->act_height = bx->height?ldiv(b->height*vbox->act_height,bx->height).quot:y_pad;
				cx += b->act_width;
				b->vx = button_get_abs_x(b) - vbox->x + vbox->vx;
				b->vy = button_get_abs_y(b) - vbox->y + vbox->vy;
			}
			fx = _max(fx,cx);
		    }
		    vbox->act_height = cy + hack;
		    vbox->act_width = fx + hack;
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
    if (cache_pix==2) for(i=0;i<kb->total_layouts;i++) cache_preload(kb,i);
    init_cnt++;
}

static int kb_switch_layout(keyboard *kb, int kbd_layout_num, int shift)
{
  box *b;
  box *b0 = kb->vvbox;

  for(; kbd_layout_num >= kb->total_layouts; kb->total_layouts++) {
	kb->kbd_layouts[kb->total_layouts] = clone_box(kb->display,kb->kbd_layouts[0],kb->total_layouts);
	if (cache_pix==2) cache_preload(kb,kb->total_layouts);
  }

  b = kb->kbd_layouts[kb->group = kbd_layout_num];
  kb->vbox = b;
  if (!shift) kb->vvbox = b;
  if (b0 == kb->vvbox) return 0;

  kb_repaint(kb);
  return 1;
}

static void kb_render(keyboard *kb)
{
	list *listp, *ip;
	if (kb->backing!=kb->win)
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

static void kb_paint(keyboard *kb)
{
  if (kb->backing!=kb->win)
  XCopyArea(kb->display, kb->backing, kb->win, kb->gc,
	    0, 0, kb->vbox->act_width, kb->vbox->act_height,
	    kb->vbox->x, kb->vbox->y);
}

void kb_repaint(keyboard *kb){
	kb_render(kb);
	kb_paint(kb);
}

void kb_resize(keyboard *kb, int width, int height)
{
	if (kb->vbox->act_width == width && kb->vbox->act_height == height) return;
	kb->vbox->act_width = width;
	kb->vbox->act_height = height;
	kb_size(kb);
	kb_repaint(kb);
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

static void _release(button *b){
#ifdef MULTITOUCH
	if (b->flags & STATE(OBIT_PRESSED))
		kb_process_keypress(b,0,STATE(OBIT_UGLY));
	else
#endif
	bdraw(b,0);
}

static void _press(button *b, unsigned int flags){
	flags |= STATE(OBIT_PRESSED);
#ifdef MULTITOUCH
	if (b->modifier & KB_STATE_KNOWN && !(b->flags & STATE(OBIT_LOCK)))
		kb_process_keypress(b,0,flags);
	else
#endif
	bdraw(b,flags);
}


#define MAX_TOUCH (1<<TOUCH_SHIFT)
#define TOUCH_MASK (MAX_TOUCH-1)
#define TOUCH_INC(x) (x=(x+1)&TOUCH_MASK)
#define TOUCH_DEC(x) (x=(x+TOUCH_MASK)&TOUCH_MASK)

button *kb_handle_events(keyboard *kb, _ushort type, const int x, const int y, const z_t z, unsigned int ptr, int dev, Time time, unsigned char *mask, int mask_len)
{
	button *b, *b1, *b2;
	int i,j;
	int n, nt;
	Time T;

typedef struct _touch {
	unsigned int touchid;
	int deviceid;
	Time time;
	button *but;
#ifdef GESTURES_EMULATE
	int x, y;
	z_t z;
	short gesture;
#ifdef MULTITOUCH
	unsigned short n;
#endif
#endif
#ifdef SIBLINGS
	button *sib[MAX_SIBLINGS];
	short nsib;
#endif
} Touch;
	static Touch touch[MAX_TOUCH];
	Touch *to;

#ifdef MULTITOUCH
	static unsigned short N=0;
	static unsigned short P=0;
	_sshort t;
	_ushort type1 = type;
#ifndef BUTTONS_TO1
	int dead;
	Time deadTime;
#endif
#endif

#ifdef BUTTONS_TO1
	// make non-touch motion single-button
	if (mask) {
		if (ptr) goto btn1;
		if (mask_len) {
			if (*mask&0x87u) goto btn1;
			for (i=1; i<mask_len; i++) if (mask[i]) goto btn1;
		}
		type = 2;
btn1:
		ptr = BUTTONS_TO1;
	}
#endif

#ifdef MULTITOUCH
	// find touch
find:
	if (type && P==N && !mask) return NULL;
#ifdef COUNT_TOUCHES
	unsigned short tcnt = 0;
	short t_old = ptr;
#endif
#ifndef BUTTONS_TO1
	dead =
#endif
	    t = -1;
//	to = NULL;
	for (i=P; i!=N; TOUCH_INC(i)) {
		Touch *t1 = &touch[i];
		if (t1->deviceid == dev) {
			j = t1->touchid;
			if (j == ptr
#ifndef BUTTONS_TO1
			    || (!ptr && type == 1 && t < 0)
#endif
			    ) {
				t = i;
				to = t1;
				if (type == 2) break;
				// duplicate (I got BEGIN!)
//				if (time==times[i] && x==X[i] && y==Y[i]) return but[i];
				if (time<=t1->time) {
					return t1->but;
				}
#ifdef BUTTONS_TO1
				break;
			} else if (j==BUTTONS_TO1 && !type) {
				// touch after emulated motion
				// first emulated motion bug: old x,y
				// reuse touch
				t = i;
				to = t1;
				break;
#endif
			}
#ifdef COUNT_TOUCHES
			else if (num_touches) {
				tcnt++;
				if (j < t_old) t_old = j;
			}
#endif
#ifndef BUTTONS_TO1
			if (mask && j<(mask_len<<3) && !(mask[j>>3] & (1 << (j & 7)))
			    && (dead < 0 || t1->time<deadTime)) {
				dead = i;
				deadTime = t1->time;
			}
#endif
		}
	}
#ifndef BUTTONS_TO1
	if (dead >= 0) {
		if (dead != t) {
			type = 3;
			to = &touch[t = dead];
			b = to->but;
			goto found;
		}
		type = 2;
	}
#endif
	if (t<0) {
		switch (type) {
		    case 0:
#ifdef COUNT_TOUCHES
			if (tcnt > num_touches) {
				to = &touch[t = t_old]; // reuse oldest overcounted
				fprintf(stderr,"input %i touch count=%i over num_touches=%i - reuse\n",dev,tcnt,num_touches);
			}
#endif
			break;
		    case 1:
			if (mask) {
				// button & motion events differ, motion enough to begin/end
				type = 0;
				break;
			}
		    default:
//			fprintf(stderr,"untracked touch on state %i\n",type);
			return NULL;
		}
#ifdef COUNT_TOUCHES
#endif
	}
#else
	const int t=0;
	to = &touch[0];
#endif
	b = kb_find_button(kb,x,y);
found:
	if (!type) { // BEGIN/press
#ifdef GESTURES_EMULATE
		if (!b && !(!mask && swipe_fingers)) return NULL;
		int g = 0;
#else
		if (!b) return NULL;
		b->z = z;
#endif
#ifdef MULTITOUCH
		if (t<0) {
			to = &touch[t = N];
			TOUCH_INC(N);
			if (N==P) TOUCH_INC(P);
		}
#endif
		to->time = time;
		to->touchid = ptr;
		to->deviceid = dev;
#ifdef GESTURES_EMULATE
		to->x = x;
		to->y = y;
		to->z = z;
#ifdef BUTTONS_TO1
		to->gesture = !b && !mask;
#else
		to->gesture = !b;
#endif
#ifdef MULTITOUCH
		if (nt = (!mask && swipe_fingers)) {
			n = N;
			TOUCH_DEC(n);
			for (i=P; i!=n; TOUCH_INC(i)) {
				Touch *t1 = &touch[i];
				if (t1->deviceid != dev) continue;
				t1->n++;
				nt++;
			}
		}
		to->n = nt;
#endif
#endif
		if (b!=(b1=to->but)) {
			to->but = b;
			if (b1) {
				if (!--b1->cnt)
					_release(b1);
#ifdef SIBLINGS
				n = to->nsib;
				// if (!b1->cnt)
					for(i=0;i<n;i++) if ((b2=to->sib[i])!=b1) _release(b2);
				to->nsib = -1;
#endif
			}
			if (!(b->cnt++)) _press(b,0);
		}
		return b;
	}

#ifdef GESTURES_EMULATE
	if (to->gesture) goto gesture;
#endif
	b1 = to->but;
#ifndef SIBLINGS
//	if (b != b1 && b1) goto drop;
	if (b) {
		// compare pressure
		if (b1 == b) {
			if (b->z < z) b->z = z;
		} else if (b1) {
			if (b1->z == z) goto drop;
			if (b1->z > z) b = b1;
			else {
				to->but = b1 = b;
				b->z = z;
			}
		}
	} else if (b1) goto drop;
#else
	/*
		intersection of sets of sibling buttons
		previous button or intersection with new button.
		if set size is 1 - this is result of touch.
		else must do other selections.
	*/
	n=to->nsib;
	if (!b) goto sib_done;
	if (b->z < z) b->z = z;
	if (!b1) { // NULL -> button: new siblings base list
		// probably never, but keep case visible
		for(i=0;i<n;i++) _release(to->sib[i]);
		to->nsib=-1;
		_press(b,STATE(OBIT_UGLY));
		to->but = b;
		b->cnt++;
	} else if (b1==b) { // first/main button - reset motions
		for(i=0;i<n;i++) if (to->sib[i]!=b) _release(to->sib[i]);
		//to->nsib=1; to->sib[0]=b;
		to->nsib=-1;
	} else if (b1->flags & STATE(OBIT_PRESSED)) { // slide from pressed
		b=b1;
	} else if (b->flags & STATE(OBIT_PRESSED)) { // pressed somewere
		b=NULL;
//	} else if (b->cnt) { // pressed somewere
//		b=NULL;
	// button -> button, invariant
	} else if (b1->z != b->z && b->z) { // try pressure
		if (b1->z > b->z) to->but = b = b1;
		else b = b1;
	} else {
		int ns, ns1 = b->nsiblings;
		button **s1 = (button **)b->siblings;
		if (n<0) {
			button **s = (button **)b1->siblings;
			ns = b1->nsiblings;
			n = 0;
			for(i=0; i<ns; i++) {
				b2=s[i];
				for(j=0; j<ns1; j++) {
					if (b2==s1[j]) {
						to->sib[n++]=b2;
//						if (!b2->cnt)
						    _press(b2,STATE(OBIT_UGLY));
						break;
					}
				}
			}
		} else {
			ns = n;
			n = 0;
			for(i=0; i<ns; i++) {
				b2=to->sib[i];
				for(j=0; j<ns1; j++) {
					if (b2==s1[j]) {
						to->sib[n++]=b2;
						b2=NULL;
						break;
					}
				}
				if (b2)
//				    if (!b2->cnt)
					_release(b2);
			}
		}
		to->nsib=n;
		if (n==0) { // no siblings, drop touch
			goto drop;
		} else if (n==1) { // 1 intersection: found button
			b1->cnt--;
			to->but = b = to->sib[0];
			b->cnt++;
		} else { // multiple, need to calculate or touch more
#if 0
			if (type==2) goto drop;
#else
			// check set of 2 buttons (first & last)
			button *s2[2] = { to->but, b };
			int n1=0;
			for(j=0; j<2; j++) {
				b2=s2[j];
				for(i=0; i<n; i++) if (to->sib[i]==b2) {
					s2[n1++]=b2;
					break;
				}
			}
			if (n1==1) {
				b1->cnt--;
				to->but = b = s2[0];
				b->cnt++;
				// in this place we can select single button, but no preview is wrong
#if 0
				// 2do? keep all set, but highlite or "press" (release)
				else if (type==1) _press(b,STATE(OBIT_UGLY)|STATE(OBIT_SELECT));
				if (type==2) to->nsib=1;
#endif
			} else b = n1 ? b1 : NULL;
#endif
		}
	}
sib_done:
#endif // SIBLINGS

	if (type==1){ // motion/to be continued
		to->time = time;
		return b;
	}

	// the END/release
	if (!b) goto drop;

	// b == b[t]

	if (b->cnt > 1) goto drop;
#ifdef SIBLINGS
	n = to->nsib;
	// on any logic, don't press without preview
	if (n > 1) goto drop;
#endif
#ifdef SLIDES
	kb_set_slide(b, x, y);
#endif
	if (b->layout_switch != -1) {
		b->flags &= ~(STATE(OBIT_PRESSED)|STATE(OBIT_UGLY));
#ifdef SIBLINGS
		for(i=0;i<n;i++) to->sib[i]->flags &= ~(STATE(OBIT_PRESSED)|STATE(OBIT_UGLY));
#endif
#ifdef MULTITOUCH
		for (i=P; i!=N; TOUCH_INC(i)) {
			touch[i].but->cnt=0;
			touch[i].but=NULL;
		}
		N=P=0;
#endif
		kb_process_keypress(b,0,0);
	} else {
		kb_process_keypress(b,0,0);
		if (b->modifier) {
			b->cnt--;
			to->but = NULL;
		}
drop:
#ifdef GESTURES_EMULATE
#ifdef MULTITOUCH
		if (!to->n || b == b1) goto drop2;
		to->gesture = 1;
gesture:
		// keep "bad gesture" touch tracked to end
		if (type != 2) return NULL;
		if (to->n != swipe_fingers) goto drop2;
		nt = 0;
		n = 0;
		for (i=P; i!=N; TOUCH_INC(i)) {
			Touch *t1 = &touch[i];
			if (t1->deviceid != dev) continue;
			n++;
			if (t1->n == to->n) nt++;
		}
		if (nt != n) goto drop2;
#else
		if (mask || b == b1 || !swipe_fingers) goto drop2;
		to->gesture = 1;
gesture:
		if (to->gesture == 99) goto drop2;
		if (type != 2) return NULL;
#endif
		int bx = 99, by = 99, xx = x - to->x, yy = y - to->y;
		if (xx<0) {xx=-xx;bx = 7;}
		else if (xx>0) bx = 6;
		if (yy<0) {yy=-yy;by = 5;}
		else if (yy>0) by = 4;
		if (xx<yy) bx = by;
		else if (xx==yy) bx = 99;
		if (to->gesture > 1 && to->gesture != bx) bx = 99;
#ifdef MULTITOUCH
		if (nt > 1) {
			if (to->gesture == bx) goto drop2;
			for (i=P; i!=N; TOUCH_INC(i)) {
				Touch *t1 = &touch[i];
				if (t1->deviceid != dev) continue;
				t1->gesture = bx;
			}
			goto drop2;
		}
#endif
		if (bx > 1 && bx != 99) {
			Display *dpy = kb->display;
			// pointers may be different, but fake event - main
			XTestFakeMotionEvent(dpy,kb->screen,kb->X+x,kb->Y+y,0);
			XTestFakeButtonEvent(dpy,bx,1,0);
			XTestFakeButtonEvent(dpy,bx,0,0);
			XFlush(dpy);
			XSync(dpy,False);
			// after gesture recognition release all
			// as possible resize & other lost touches
#ifdef MULTITOUCH
			for (j=P; j!=N; TOUCH_INC(j)) {
				to = &touch[j];
				_release(b=to->but);
				n = to->nsib;
				for(i=0;i<n;i++) if ((b1=to->sib[i])!=b) _release(b1);
				to->nsib=-1;
			}
			N=P=0;
#endif
			return NULL;
		}
drop2:
#endif
		if (b=to->but) {
			to->but = NULL;
			if (!--b->cnt)
				_release(b);
#ifdef SLIDES
			b->slide = 0;
#endif
		}
#ifdef MULTITOUCH
		TOUCH_DEC(N);
#ifdef SIBLINGS
		n = to->nsib;
		for(i=0;i<n;i++) if ((b1=to->sib[i])!=b) _release(b1);
		to->nsib=-1;
#endif
		if (t!=N) touch[t] = touch[N];
	}
	if (type != 3) return NULL;
	type=type1;
	XSync(kb->display,False);
	goto find;
#else
	}
	return NULL;
#endif
}

#ifdef SLIDES
static void kb_set_slide(button *b, int x, int y)
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
static void kb_process_keypress(button *b, int repeat, unsigned int flags)
{
    keyboard *kb = b->kb;
    unsigned int state = kb->state;
    unsigned int lock = kb->state_locked;
    const unsigned int mod = b->modifier;
    int layout = b->layout_switch;
    int keypress = 1;
    unsigned int st,st0 = 0;
    Display *dpy = kb->display;

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
	if (Xkb_sync==2) {
		XkbStateRec s;
		XFlush(dpy);
		XSync(dpy,False);
		XkbGetState(dpy,XkbUseCoreKbd,&s);
		kb_sync_state(b->kb,s.mods,s.locked_mods,layout>=0?layout:s.group);
		return;
	}
    }
    if (state != kb->state || lock != kb->state_locked) {
#ifndef MINIMAL
	if (Xkb_sync) {
		XSync(dpy,False); // serialize
		if (st != kb->state & KB_STATE_KNOWN) XkbLatchModifiers(dpy,XkbUseCoreKbd,KB_STATE_KNOWN,st);
		if (st != kb->state_locked & KB_STATE_KNOWN) XkbLockModifiers(dpy,XkbUseCoreKbd,KB_STATE_KNOWN,st);
//		if ((state^kb->state ^ state)&KB_STATE_KNOWN) XkbLatchModifiers(dpy,XkbUseCoreKbd,KB_STATE_KNOWN,state & KB_STATE_KNOWN);
//		if ((lock^kb->state_locked ^ lock)&KB_STATE_KNOWN) XkbLockModifiers(dpy,XkbUseCoreKbd,KB_STATE_KNOWN,lock & KB_STATE_KNOWN);
//		XSync(dpy,True); // reduce events
	}
#endif
	kb->state = state;
	kb->state_locked = lock;
	kb_repaint(kb);
   }

   if (layout!=-1) {
    if (layout>-1) {
#ifndef MINIMAL
	if (Xkb_sync) {
		XSync(dpy,False);
		XkbLockGroup(dpy, XkbUseCoreKbd, b->layout_switch);
		//XSync(dpy,True);
		XSync(dpy,False);
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
#define _xsync(x) if (Xkb_sync==1) XSync(dpy,x)
#else
#define _xsync(x)
#endif

static unsigned int saved_mods = 0;
void kb_send_keypress(button *b, unsigned int next_state, unsigned int flags) {
#ifdef SLIDES
	unsigned int l = b->slide?:KBLEVEL(b);
#else
	unsigned int l = KBLEVEL(b);
#endif
	unsigned int mods, mods0;
	Display *dpy = b->kb->display;

#ifndef MINIMAL
	if (Xkb_sync) {
		mods = b->mods[l];
		mods0 = b->kb->state_locked & KB_STATE_KNOWN;
	} else
#endif
	{
		mods0 = saved_mods;
		mods = b->kb->state|b->kb->state_locked;
		saved_mods = next_state = b->kb->state_locked & KB_STATE_KNOWN;
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
		XTestFakeKeyEvent(dpy, b->kc[l], True, 0);
		_xsync(True);
		b->flags |= STATE(OBIT_PRESSED);
	}
	if (!(flags & STATE(OBIT_PRESSED))) {
		XTestFakeKeyEvent(dpy, b->kc[l], False, 0);
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
		if (y >= i+bx->act_height && listp->next) continue;
		for(ip=bx->root_kid; ip; ip=ip->next) {
			tmp_but = (button *)ip->data;
			i = tmp_but->x+bx->x;
			if (x<i) break;
			if (x>=i+tmp_but->act_width && ip->next) continue;
//			i = tmp_but->y+bx->y
//			if (y<i) break;
//			if (y>i+tmp_but->act_height && listp->next) continue;
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
