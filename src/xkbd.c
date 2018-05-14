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
#include <unistd.h>
#include <signal.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/shape.h>
#include "../config.h"

#include "libXkbd.h"

#define DEBUG 1

#define WIN_NORMAL  1
#define WIN_OVERIDE 2

#define WIN_OVERIDE_AWAYS_TOP 0
#define WIN_OVERIDE_NOT_AWAYS_TOP 1

Display* display; /* ack globals due to sighandlers - another way ? */
Window   win;
int      screen_num;

int Xkb_sync = 0;
int no_lock = 0;
XkbStateRec Xkb_state[1] = {};

enum {
  WM_UNKNOWN,
  WM_EHWM_UNKNOWN,
  WM_METACITY,
  WM_MATCHBOX,
};

static unsigned char*
get_current_window_manager_name (void)
{
  Atom utf8_string, atom, atom_check, type;
  int result;
  unsigned char *retval;
  int format;
  long nitems;
  long bytes_after;
  unsigned char *val;
  Window *xwindow = NULL;

  atom_check = XInternAtom (display, "_NET_SUPPORTING_WM_CHECK", False);

  XGetWindowProperty (display,
		      RootWindow(display, DefaultScreen(display)),
		      atom_check,
		      0, 16L, False, XA_WINDOW, &type, &format,
		      &nitems, &bytes_after, (unsigned char **)&xwindow);

  if (xwindow == NULL)
      return NULL;


  utf8_string = XInternAtom (display, "UTF8_STRING", False);
  atom = XInternAtom (display, "_NET_WM_NAME", False);

  result = XGetWindowProperty (display,
		  	       *xwindow,
			       atom,
			       0, 1000L,
			       False, utf8_string,
			       &type, &format, &nitems,
			       &bytes_after, (unsigned char **)&val);

  if (result != Success)
    return NULL;

  if (type != utf8_string || format !=8 || nitems == 0)
    {
      if (val) XFree (val);
      return NULL;
    }

  retval = strdup (val);

  XFree (val);

  return retval;
}


void handle_sig(int sig)
{
   XWindowAttributes attr;
   XGetWindowAttributes(display, win, &attr);
   if (attr.map_state == IsUnmapped ||
       attr.map_state == IsUnviewable )
   {
      XMapWindow(display, win);
      XRaiseWindow(display,win);
   } else {
      XUnmapWindow(display, win);
   }
}

void version()
{
   printf("Version: %s \n", VERSION);
#ifdef USE_XFT
   printf("XFT supported\n");
#endif
#ifdef USE_XPM
   printf("XPM supported\n");
#endif
}

void usage(void)
{
   printf("Usage: xkbd <options>\n\
Options:\n\
  -display  <display>\n\
  -geometry <geometry>\n\
     ( NOTE: The above will overide the configs font )\n\
  -k  <keyboard file> Select the keyboard definition file\n\
                      other than" DEFAULTCONFIG "\n\
  -xid used for gtk embedding\n\
  -c  Dock\n\
  -s  strut\n\
  -X  Xkb state interaction\n\
  -l  disable modifiers lock\n\
  -v  version\n\
  -h  this help\n");
#ifdef USE_XFT
   printf("  -font <font name>  Select the xft AA font for xkbd\n");
#else
   printf("  -font <font name>  Select the X11 font for xkbd\n");
#endif
}

int main(int argc, char **argv)
{
   char *window_name = "xkbd";

   char *icon_name = "xkbd";

#define PROP_MOTIF_WM_HINTS_ELEMENTS    5
#define MWM_HINTS_DECORATIONS          (1L << 1)
#define MWM_DECOR_BORDER               (1L << 1)

   typedef struct
   {
     unsigned long       flags;
     unsigned long       functions;
     unsigned long       decorations;
     long                inputMode;
     unsigned long       status;
   } PropMotifWmHints ;

   PropMotifWmHints *mwm_hints;

   XSizeHints size_hints;
   XWMHints *wm_hints;

   char *display_name = (char *)getenv("DISPLAY");

   Xkbd *kb = NULL;

   char *wm_name;
   int wm_type = WM_UNKNOWN;

   char *geometry = NULL;
   int xret=0, yret=0, wret=0, hret=0;
   char *conf_file = NULL;
   char *font_name = NULL;
   int cmd_xft_selected = 0; /* ugly ! */
   int embed = 0;
   int dock = 0;
   Bool use_normal_win = False;

   XEvent ev;
   int  xkbEventType = Expose; // any used berfore

   int i;
   char userconffile[256];
   FILE *fp;
   KeySym mode_switch_ksym;



   for (i=1; argv[i]; i++) {
      char *arg = argv[i];
      if (*arg=='-') {
	 switch (arg[1]) {
	    case 'd' : /* display */
	       display_name = argv[i+1];
	       i++;
	       break;
	    case 'g' :
	       geometry = argv[i+1];
	       i++;
	       break;
	    case 'f':
	       font_name = argv[i+1];
#ifdef USE_XFT
	       cmd_xft_selected = 1;
#endif
	       break;
	    case 'o' : /* wm override */
	    case 't' :
	       fprintf( stderr, "Overide redirect support deprciated\n");
	       exit(1);
	       break;
	    case 'k' :
	       conf_file = argv[i+1];
	       i++;
	       break;
	    case 'x' :
	       embed = 1;
	       break;
	    case 'c' :
	       dock |= 1;
	       break;
	    case 's' :
	       dock |= 2;
	       break;
	    case 'X' :
	       Xkb_sync = 1;
	       break;
	    case 'l' :
	       no_lock = 1;
	       break;
	    case 'n' :
	       use_normal_win = True;
	       break;
	    case 'v' :
	       version();
	       exit(0);
	       break;
	    default:
	       usage();
	       exit(0);
	       break;
	 }
      }
   }

   if (Xkb_sync) {
	int xkbError, reason_rtrn, mjr = XkbMajorVersion, mnr = XkbMinorVersion;

	display = XkbOpenDisplay(display_name, &xkbEventType, &xkbError, &mjr, &mnr, &reason_rtrn);
	if (!display) goto no_dpy;
	/* vs. some annoying X errors, reset state on start in sync mode */
	XkbLockModifiers(display,XkbUseCoreKbd,0xffff,0);
	XkbLatchModifiers(display,XkbUseCoreKbd,0xffff,0);
//	XkbSelectEvents(display,XkbUseCoreKbd,XkbAllEventsMask,XkbAllEventsMask);
	XkbSelectEvents(display,XkbUseCoreKbd,XkbStateNotifyMask,XkbStateNotifyMask);
   } else {
	display = XOpenDisplay(display_name);
	if (!display) goto no_dpy;
   }

      Atom wm_protocols[]={
	 XInternAtom(display, "WM_DELETE_WINDOW",False),
	 XInternAtom(display, "WM_PROTOCOLS",False),
	 XInternAtom(display, "WM_NORMAL_HINTS", False),
      };

      Atom window_type_toolbar_atom =
	 XInternAtom(display, "_NET_WM_WINDOW_TYPE_TOOLBAR",False);
      Atom mwm_atom =
	XInternAtom(display, "_MOTIF_WM_HINTS",False);

      /* HACK to get libvirtkeys to work without mode_switch */

      screen_num = DefaultScreen(display);

      if  (XKeysymToKeycode(display, XK_Mode_switch) == 0)
	{
	  int keycode;
	  int min_kc, max_kc;

	  XDisplayKeycodes(display, &min_kc, &max_kc);

	  for (keycode = min_kc; keycode <= max_kc; keycode++)
	    if (XkbKeycodeToKeysym (display, keycode, 0, 0) == NoSymbol)
	      {
		mode_switch_ksym = XStringToKeysym("Mode_switch");
		XChangeKeyboardMapping(display,
				       keycode, 1,
				       &mode_switch_ksym, 1);
		XSync(display, False);
	      }
      }

      wm_name = get_current_window_manager_name ();
      use_normal_win = True;

      if (wm_name)
	{
	  wm_type = WM_EHWM_UNKNOWN;
	  if (!strcmp(wm_name, "metacity"))
	    wm_type = WM_METACITY;
	  else if (!strcmp(wm_name, "matchbox"))
	    {

	      use_normal_win = False;
	      wm_type = WM_MATCHBOX;
	    }
	}

      win = XCreateSimpleWindow(display,
				RootWindow(display, screen_num),
				0, 0,
				300, 300,
				0, BlackPixel(display, screen_num),
				WhitePixel(display, screen_num));

      wret = DisplayWidth(display, screen_num);
      hret = DisplayHeight(display, screen_num)/4;
      xret = 0;
      yret = DisplayHeight(display, screen_num) - hret;

      // deactivate geometry for 0.8.16 release - mb
//      geometry = NULL;
      if (geometry != NULL)
	{
	  int flags;

	  flags = XParseGeometry(geometry, &xret, &yret, &wret, &hret );
	  if( flags & XNegative )
	      xret += DisplayWidth( display, screen_num ) - wret;
	  if( flags & YNegative )
	      yret += DisplayHeight( display, screen_num ) - hret;
	}

      /* check for user selected keyboard conf file */

      if (conf_file == NULL)
	{
	  strcpy(userconffile,getenv("HOME"));
	  strcat(userconffile, "/.xkbd");

	  if ((fp = fopen(userconffile, "r")) != NULL)
	    {
	      conf_file = (char *)malloc(sizeof(char)*512);
	      if (fgets(conf_file, 512, fp) != NULL)
		{
		  fclose(fp);
		  if ( conf_file[strlen(conf_file)-1] == '\n')
		    conf_file[strlen(conf_file)-1] = '\0';
		}
	    }
	  else
	    {
	      conf_file = DEFAULTCONFIG;
	    }
	}

      kb = xkbd_realize(display, win, conf_file, font_name, 0, 0,
			wret, hret, cmd_xft_selected);

      XResizeWindow(display, win, xkbd_get_width(kb), xkbd_get_height(kb));

      if (xret || yret)
	 XMoveWindow(display,win,xret,yret);

      size_hints.flags = PPosition | PSize | PMinSize;
      size_hints.x = 0;
      size_hints.y = 0;
      size_hints.width      =  xkbd_get_width(kb);
      size_hints.height     =  xkbd_get_height(kb);
      size_hints.min_width  =  xkbd_get_width(kb);
      size_hints.min_height =  xkbd_get_height(kb);

      XSetStandardProperties(display, win, window_name,
			     icon_name, 0,
			     argv, argc, &size_hints);

      wm_hints = XAllocWMHints();
      wm_hints->input = False;
      wm_hints->flags = InputHint;
      if (dock & 1) {
	wm_hints->flags |= IconWindowHint | WindowGroupHint | StateHint;
	wm_hints->initial_state = WithdrawnState;
	wm_hints->icon_window = wm_hints->window_group = win;
      }
      XSetWMHints(display, win, wm_hints );

      /* Tell the WM we dont want no borders */
      mwm_hints = calloc(1, sizeof(PropMotifWmHints));

      mwm_hints->flags = MWM_HINTS_DECORATIONS;
      mwm_hints->decorations = 0;



      XChangeProperty(display, win, mwm_atom,
		      XA_ATOM, 32, PropModeReplace,
		      (unsigned char *)mwm_hints,
		      PROP_MOTIF_WM_HINTS_ELEMENTS);

      free(mwm_hints);


      XSetWMProtocols(display, win, wm_protocols, sizeof(wm_protocols) /
		      sizeof(Atom));

      if (use_normal_win == False)
	XChangeProperty(display, win,
		XInternAtom(display, "_NET_WM_WINDOW_TYPE" , False),
		XA_ATOM, 32, PropModeReplace,
		(unsigned char *) &window_type_toolbar_atom, 1);

      if (dock & 2) {
	/* learned from tint2 */
	long prop[12] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	XChangeProperty(display,win, mwm_atom, mwm_atom,32,PropModeReplace,(unsigned char *)&prop,5);
        prop[0]=0;
	prop[3] = xkbd_get_height(kb); // -1 ?
	XChangeProperty(display,
		win,
		XInternAtom(display, "_NET_WM_STRUT",False),
		XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&prop, 4);
	XChangeProperty(display, win,
		XInternAtom(display, "_NET_WM_STRUT_PARTIAL",False),
		XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&prop, 12);

      }

      if (embed)
      {
	 fprintf(stdout, "%li\n", win);
	 fclose(stdout);
      } else {
	 XMapWindow(display, win);
      }

      signal(SIGUSR1, handle_sig); /* for extenal mapping / unmapping */

      XSelectInput(display, win,
		   ExposureMask |
		   ButtonPressMask |
		   ButtonReleaseMask |
		   Button1MotionMask |
		   StructureNotifyMask |
		   VisibilityChangeMask);

      while (1)
      {
	    XNextEvent(display, &ev);
	    xkbd_process(kb, &ev);
	    switch (ev.type) {
	    case ClientMessage:
		  if ((ev.xclient.message_type == wm_protocols[1])
		      && (ev.xclient.data.l[0] == wm_protocols[0]))
		  {
			xkbd_destroy(kb);
			XCloseDisplay(display);
			exit(0);
		  }
		  break;
		case ConfigureNotify:
		  if ( ev.xconfigure.width != xkbd_get_width(kb)
		       || ev.xconfigure.height != xkbd_get_height(kb))
		  {
		     xkbd_resize(kb,
				 ev.xconfigure.width,
				 ev.xconfigure.height );
		  }
		  break;
		case Expose:
		  xkbd_repaint(kb);
		  break;
		default: if (ev.type == xkbEventType) {
			switch (((XkbEvent)ev).any.xkb_type) {
			    case XkbStateNotify:
				if (xkbd_sync_state(kb))
					xkbd_repaint(kb);
				break;
			}
		}
	    }
	    while (xkbd_process_repeats(kb) && !XPending(display))
		usleep(10000L); /* sleep for a 10th of a second */
      }
no_dpy:
	fprintf(stderr, "%s: cannot connect to X server '%s'\n", argv[0], display_name);
	exit(1);
}


