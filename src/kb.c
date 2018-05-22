/*
   xkbd - xlib based onscreen keyboard.

   Copyright (C) 2001 Matthew Allum

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

#include "libvirtkeys.h"

#ifdef DEBUG
#define DBG(txt, args... ) fprintf(stderr, "DEBUG" txt "\n", ##args )
#else
#define DBG(txt, args... ) /* nothing */
#endif

#define TRUE  1
#define FALSE 0

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
	const unsigned int mods[4] = {0,STATE(KBIT_SHIFT),STATE(KBIT_MOD),STATE(KBIT_SHIFT)|STATE(KBIT_MOD)};
	int n;
	char *txt;

	for(l=0; l<4; l++){
		KeySym ks = b->ks[l];

		if(!ks && (ks = b->ks[l&2]?:b->ks[0]))
			XkbTranslateKeySym(dpy,&ks,mods[l],buf,1,&n);
		if (!(txt = b->txt[l])) {
			for (l1 = 0; l1<l && !txt; l1++) if (ks == b->ks[l1]) txt = b->txt[l1];
			ksText_(ks,&txt);
			b->txt[l] = txt;
		}
		b->ks[l] = ks;
	}
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
  kb->gc = _createGC(display, win);
  XSetForeground(display, kb->gc,
		 BlackPixel(display, DefaultScreen(display) ));
  XSetBackground(display, kb->gc,
		 WhitePixel(display, DefaultScreen(display) ));

  kb->rev_gc = _createGC(display, win);
  XSetForeground(display, kb->rev_gc,
		 WhitePixel(display, DefaultScreen(display) ));
  XSetBackground(display, kb->rev_gc,
		 BlackPixel(display, DefaultScreen(display) ));

  kb->txt_gc = _createGC(display, win);
  XSetForeground(display, kb->txt_gc,
		 BlackPixel(display, DefaultScreen(display) ));
  XSetBackground(display, kb->txt_gc,
		 WhitePixel(display, DefaultScreen(display) ));

  kb->txt_rev_gc = _createGC(display, win);
  XSetForeground(display, kb->txt_rev_gc,
		 WhitePixel(display, DefaultScreen(display) ));
  XSetBackground(display, kb->rev_gc,
		 BlackPixel(display, DefaultScreen(display) ));

  kb->bdr_gc = _createGC(display, win);
  XSetForeground(display, kb->bdr_gc,
		 BlackPixel(display, DefaultScreen(display) ));
  XSetBackground(display, kb->bdr_gc,
		 WhitePixel(display, DefaultScreen(display) ));

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

  setupKeyboardVariables(kb->display);

  if ((rcfp = fopen(conf_file, "r")) == NULL)
    {
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
		  {
		    XColor col;
		    if (_XColorFromStr(kb->display, &col, tmpstr_C) == 0)
		      {
			perror("color allocation failed\n"); exit(1);
		      }
		    XSetForeground(kb->display, kb->rev_gc, col.pixel );
		  }
		else if (strcmp(tmpstr_A, "border_col") == 0)
		  {
		    XColor col;
		    if (_XColorFromStr(kb->display, &col, tmpstr_C) == 0)
		      {
			perror("color allocation failed\n"); exit(1);
		      }
		    XSetForeground(kb->display, kb->bdr_gc, col.pixel );
		  }
		else if (strcmp(tmpstr_A, "down_col") == 0)
		  {
		    XColor col;
		    if (_XColorFromStr(kb->display, &col, tmpstr_C) == 0)
		      {
			perror("color allocation failed\n"); exit(1);
		      }
		    XSetForeground(kb->display, kb->gc, col.pixel );
		  }
		else if (strcmp(tmpstr_A, "width") == 0)
		  {
		    /* TODO fix! seg's as kb->vbox does not yet exist
		     if (!kb->vbox->act_width)
			kb->vbox->act_width = atoi(tmpstr_C);
		    */
		  }
		else if (strcmp(tmpstr_A, "height") == 0)
		  {
		    /* TODO fix! seg's as kb->vbox does not yet exist
		     if (!kb->vbox->act_height)
			kb->vbox->act_height = atoi(tmpstr_C);
		    */

                    height_tmp = atoi(tmpstr_C);
		  }
#ifdef SLIDES
		else if (strcmp(tmpstr_A, "slide_margin") == 0)
		  {
		     kb->slide_margin = atoi(tmpstr_C);
		  }
#endif
		else if (strcmp(tmpstr_A, "repeat_delay") == 0)
		  {
		     kb->key_delay_repeat = atoi(tmpstr_C);
		  }
		else if (strcmp(tmpstr_A, "repeat_time") == 0)
		  {
		     kb->key_repeat = atoi(tmpstr_C);
		  }

		else if (strcmp(tmpstr_A, "txt_col") == 0)
		  {
		    XColor col;
		    if (_XColorFromStr(kb->display, &col, tmpstr_C) == 0)
		      {
			perror("color allocation failed\n"); exit(1);
		      }
#ifdef USE_XFT
		    if (kb->render_type == oldskool)
		      {
#endif
			XSetForeground(kb->display, kb->txt_gc, col.pixel );
#ifdef USE_XFT
		      }
		    else
		      {

			colortmp.red   = col.red;
			colortmp.green = col.green;
			colortmp.blue  = col.blue;
			colortmp.alpha = 0xFFFF;
			XftColorAllocValue(display,
					   DefaultVisual(display,
						      DefaultScreen(display)),
					   DefaultColormap(display,
						      DefaultScreen(display)),
					   &colortmp,
					   &kb->color_fg);
		      }
#endif
		  }

		break;
	      case rowdef: /* no rowdefs as yet */
		break;
	      case keydef:
		if (strcmp(tmpstr_A, "default") == 0)
		  DEFAULT_TXT(tmp_but) = button_set(tmpstr_C);
		else if (strcmp(tmpstr_A, "shift") == 0)
		  SHIFT_TXT(tmp_but) = button_set(tmpstr_C);
		else if (strcmp(tmpstr_A, "switch") == 0)
		  button_set_layout(tmp_but, tmpstr_C);
		else if (strcmp(tmpstr_A, "mod") == 0)
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
		  { button_set_bg_col(tmp_but, tmpstr_C); }
		else if (strcmp(tmpstr_A, "fg") == 0)
		  { button_set_fg_col(tmp_but, tmpstr_C); }
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
		   tmp_but->options |= STATE(OBIT_WIDTH_SPEC);
		    tmp_but->c_width = atoi(tmpstr_C);
		}
		else if (strcmp(tmpstr_A, "key_span_width") == 0)
		{
		   tmp_but->options |= STATE(OBIT_WIDTH_SPEC);
		   tmp_but->key_span_width = atoi(tmpstr_C);
		}

		else if (strcmp(tmpstr_A, "height") == 0)
		  tmp_but->c_height = atoi(tmpstr_C);
                else if (strcmp(tmpstr_A, "obey_capslock") == 0)
		{
		  if (strcmp(tmpstr_C, "yes") == 0)
		    tmp_but->options |= STATE(OBIT_OBEYCAPS);
		  else if (strcmp(tmpstr_C, "no") == 0)
		    tmp_but->options &= ~STATE(OBIT_OBEYCAPS);
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
	   if (!(b->options & STATE(OBIT_WIDTH_SPEC)))
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
      kb->vbox = kb->kbd_layouts[kb->group = j];
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
	      if (!(b->options & STATE(OBIT_WIDTH_SPEC)))
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

}

void
kb_switch_layout(keyboard *kb, int kbd_layout_num)
{
  int w = kb->vbox->act_width;
  int h = kb->vbox->act_height;
  int mw = kb->vbox->min_width;
  int mh = kb->vbox->min_height;

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
			button_render(tmp_but, (tmp_but->modifier & kb->state_locked)?BUTTON_LOCKED:(tmp_but->modifier & kb->state)?BUTTON_PRESSED:BUTTON_RELEASED);
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

button *kb_handle_events(keyboard *kb, XEvent an_event)
{
  static button *active_but;

  switch (an_event.type)
    {
      case ButtonPress:
	active_but = kb_find_button(kb,
				    an_event.xmotion.x,
				    an_event.xmotion.y );
	if (active_but != NULL)
	  {
	    button_render(active_but, BUTTON_PRESSED);
	    button_paint(active_but);
	    /* process state here
	       send keypress via kbd state
	    */
	  }
	break;
      case ButtonRelease:
	if (active_but != NULL)
	  {
	    int new_state;

#ifdef SLIDES
	    kb_set_slide(active_but, an_event.xmotion.x,
			 an_event.xmotion.y );
#endif
	    new_state = kb_process_keypress(active_but);

	    if (new_state != active_but->kb->state ||
		new_state & active_but->modifier)
	      {
		/* if the states changed repaint the entire kbd
		   as its chars have probably changed */

		active_but->kb->state = new_state;
		kb_render(active_but->kb);
		kb_paint(active_but->kb);
	      } else {
		button_render(active_but, BUTTON_RELEASED);
		button_paint(active_but);
	      }
	    /* check for slide */

#ifdef SLIDES
	    active_but->slide = 0;
#endif
	    if (active_but->layout_switch > -1)
	      {
		DBG("switching layout\n");
#ifndef MINIMAL
		if (Xkb_sync)
			XkbLockGroup(active_but->kb->display, XkbUseCoreKbd, active_but->layout_switch);
		else
#endif
			kb_switch_layout(active_but->kb, active_but->layout_switch);
	      }

	    active_but = NULL;
	  }
	break;
    }

  return active_but;
}

#ifdef SLIDES
void kb_set_slide(button *active_but, int x, int y)
{
  if (x < (button_get_abs_x(active_but)-active_but->kb->slide_margin))
    { active_but->slide = SLIDE_LEFT; return; }

  if (x > ( button_get_abs_x(active_but)
	    + active_but->act_width + -active_but->kb->slide_margin ))
    { active_but->slide = SLIDE_RIGHT; return; }

  if (y < (button_get_abs_y(active_but)-active_but->kb->slide_margin))
    { active_but->slide = SLIDE_UP; return; }

  if (y > ( button_get_abs_y(active_but) + active_but->act_height )
      + -active_but->kb->slide_margin )
    { active_but->slide = SLIDE_DOWN; return; }


}
#endif

Bool kb_do_repeat(keyboard *kb, button *active_but)
{
  static int timer;
  static Bool delay;

  if (!kb->key_repeat)
    return False;
  if (active_but == NULL)
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
      kb_process_keypress(active_but);
      timer = 0;
      delay = True;
    }
    return True;
}

int kb_process_keypress(button *active_but)
{
    unsigned int state = active_but->kb->state;
    unsigned int lock = active_but->kb->state_locked;
    const unsigned int mod = active_but->modifier;
    int keypress = 1;

    DBG("got release state %i %i %i %i \n", new_state, STATE(KBIT_SHIFT), STATE(KBIT_MOD), STATE(KBIT_CTRL) );

    if (mod & STATE(KBIT_CAPS)) {
	state ^= STATE(KBIT_CAPS);
	DBG("got caps key - %i \n", state);
    } else if (mod) {
	if (lock & mod) {
		lock ^= mod;
		state ^= mod;
		keypress = 0;
	} else if ((state & mod)!=mod) {
		state ^= mod;
	} else if (no_lock) {
		state ^= mod;
		keypress = 0; /* do not activate grp:ctrl_shift_toggle */
	} else {
		lock ^= mod;
		keypress = 0;
	}
	DBG("got a modifier key - %i \n", state);
    } else if (state & ~STATE(KBIT_CAPS)) {
	/* check if the kbd is already in a state and reset it
	   leaving caps key state alone */
	state &= STATE(KBIT_CAPS);
	state |= lock;
	DBG("kbd is shifted, unshifting - %i \n", state);
    }

    if (mod & ~STATE(KBIT_CAPS)) {
	/* strange ("unKNOWN") combinations do "X Error"
	   keep them virtual & change whole KNOWN mask only */
#ifndef MINIMAL
	if (Xkb_sync && (mod & KB_STATE_KNOWN)) {
		if (state != active_but->kb->state)
			XkbLatchModifiers(active_but->kb->display,XkbUseCoreKbd,KB_STATE_KNOWN,state & KB_STATE_KNOWN);
		if (lock != active_but->kb->state_locked)
			XkbLockModifiers(active_but->kb->display,XkbUseCoreKbd,KB_STATE_KNOWN,lock & KB_STATE_KNOWN);
	}
#endif
	active_but->kb->state_locked = lock;
    }

    if (keypress) {
	kb_send_keypress(active_but);
	DBG("%s clicked \n", DEFAULT_TXT(active_but));
    }

    /* real precise state for Xkb_sync will be reached by event,
       so try to be just visually pretty sensitive */
    return state;
}

void kb_send_keypress(button *b)
{
  int slide_flag = 0;
  unsigned int l = KBLEVEL(b->kb);
  unsigned int l1 = l;
  unsigned int l2 = 0;
  KeySym ks = GET_KS(b,l);

  struct keycodeEntry vk_keycodes[10];

#ifdef SLIDES
  if (b->slide) { // 2do grok slides ;)
      ks = GET_KS(b,l1 = b->slide);
      switch (b->slide)
	{
	  case SLIDE_UP :
//	    if (ks == 0) ks = SHIFT_KS(b);
	    if (ks == 0 && (b->kb->state & STATE(KBIT_SHIFT))) ks = SHIFT_KS(b);
	    break;
	  case SLIDE_DOWN : /* hold ctrl */
//	    if (ks == 0) slide_flag = STATE(KBIT_CTRL);
	    if (!ks && (b->kb->state & STATE(KBIT_CTRL))) slide_flag = STATE(KBIT_CTRL);
	    break;
	  case SLIDE_LEFT : /* hold alt */
//	    if (ks == 0)
	    if (ks == 0 && (b->kb->state & STATE(KBIT_MOD)))
	      {
		ks = MOD_KS(b);
//		l1 = 2;
		slide_flag = STATE(KBIT_MOD);
	      }
	    break;
	  case SLIDE_RIGHT : /* hold alt */
	    break;
	}
  }
#endif
  if (ks == 0) ks = DEFAULT_KS(b);
  if (ks == 0) return; /* no keysym defined, abort */

#ifdef SEQ_CACHE
  struct keycodeEntry *kcs = b->cache[l1];
  int len;

  if (!kcs && (len = lookupKeyCodeSequence(ks, vk_keycodes, NULL, b->kb->group, l, !((b->kb->state & STATE(KBIT_SHIFT))==0))))
	memcpy(b->cache[l1] = kcs = malloc(len),&vk_keycodes,len);
  if (kcs)
     sendKeySequence(kcs,
#else
  if (lookupKeyCodeSequence(ks, vk_keycodes, NULL, b->kb->group, l, !((b->kb->state & STATE(KBIT_SHIFT))==0)))
     sendKeySequence(vk_keycodes,
#endif
	  ( (b->kb->state & STATE(KBIT_CTRL))  || (slide_flag == STATE(KBIT_CTRL)) ),
	  ( (b->kb->state & STATE(KBIT_META))  || (slide_flag == STATE(KBIT_META)) ),
	  ( (b->kb->state & STATE(KBIT_ALT))   || (slide_flag == STATE(KBIT_ALT))  ),
		     0 /* ( (b->kb->state & STATE(KBIT_SHIFT)) || (slide_flag == STATE(KBIT_SHIFT)) ) */
		  );

}



button * kb_find_button(keyboard *kb, int x, int y)
{
  list *listp;
  box *tmp_box = NULL;
  int offset_x, offset_y;
  button *but = NULL;

  offset_x = kb->vbox->x;
  offset_y = kb->vbox->y;

  /* global check required(?) only on global event hook. we dont */
//  if (x >= offset_x &&
//      y >= offset_y &&
//      x <= (offset_x+kb->vbox->act_width) &&
//      y <= (offset_y+kb->vbox->act_height) )
//    {
      listp = kb->vbox->root_kid;
      while (listp != NULL)
	{
	  list *ip;

	  button *tmp_but = NULL;
	  tmp_box = (box *)listp->data;
	  if (y >= (offset_y + tmp_box->y) &&
	      y <= (offset_y + tmp_box->y + tmp_box->act_height))
	    {
	      ip = tmp_box->root_kid;
	      while (ip != NULL) /* now the row is found, find the key */
		{
		  tmp_but = (button *)ip->data;
		  if (x >= (tmp_but->x+offset_x+tmp_box->x) &&
		      x <= (tmp_but->x+offset_x+tmp_box->x+tmp_but->act_width) &&
		      y >= (tmp_but->y+offset_y+tmp_box->y) &&
		      y <= (tmp_but->y+offset_y+tmp_box->y+tmp_but->act_height)
		      )
		    {
			/* if pressed invariant/border - check buttons are identical */
			if (but && memcmp(but,tmp_but,(char*)&but->options - (char*)&but->ks) + sizeof(but->options)
				)
				return NULL;
		        but = tmp_but;
		    }

		  ip = ip->next;
		}
	    }
	  listp = listp->next;
	}
//    }
  if (!but)
	fprintf(stderr, "xkbd: no button %i,%i\n",x,y);
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
