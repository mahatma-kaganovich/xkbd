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
#include <unistd.h>
#include <signal.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/shape.h>

#ifdef USE_XI
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#endif

#include "../config.h"

#include "libXkbd.h"

#define DEBUG 1

#define WIN_NORMAL  1
#define WIN_OVERIDE 2

#define WIN_OVERIDE_AWAYS_TOP 0
#define WIN_OVERIDE_NOT_AWAYS_TOP 1

Display* display; /* ack globals due to sighandlers - another way ? */
Window   win;
Window rootWin;
Atom mwm_atom;
int screen_num;

#ifndef MINIMAL
int Xkb_sync = 0;
#endif
int no_lock = 0;

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

  XGetWindowProperty (display,rootWin,XInternAtom (display, "_NET_SUPPORTING_WM_CHECK", False),
		      0, 16L, False, XA_WINDOW, &type, &format,
		      &nitems, &bytes_after, (unsigned char **)&xwindow);

  if (xwindow == NULL)
      return NULL;


  utf8_string = XInternAtom (display, "UTF8_STRING", False);

  result = XGetWindowProperty (display,*xwindow,XInternAtom (display, "_NET_WM_NAME", False),
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

void _reset(int sig){
   XkbLockModifiers(display,XkbUseCoreKbd,0xffff,0);
   XkbLatchModifiers(display,XkbUseCoreKbd,0xffff,0);
   XkbLockGroup(display,XkbUseCoreKbd,0);
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
  -c  dock\n\
  -s  strut\n\
  -D  Dock/options bitmask: 1=dock, 2=strut, 4=_NET_WM_WINDOW_TYPE_DOCK,\n\
      8=_NET_WM_WINDOW_TYPE_TOOLBAR, 16=_NET_WM_STATE_STICKY.\n\
      For OpenBox I use 22 = $[2+4+16].\n\
  -X  Xkb state interaction\n\
  -l  disable modifiers lock\n\
  -v  version\n\
  -e {<...>}   exec on restart (to end of line)\n\
  -h  this help\n");
#ifdef USE_XFT
   printf("  -font <font name>  Select the xft AA font for xkbd\n");
#else
   printf("  -font <font name>  Select the X11 font for xkbd\n");
#endif
}

void _prop(int i, char *prop, Atom type, void *data, int n){
	XChangeProperty(display,win,prop?XInternAtom(display,prop,False):mwm_atom,type,i,PropModeReplace,(unsigned char *)data,n); 
}

void _propAtom32(char *prop, char *data){
	Atom a=XInternAtom(display,data,False);
	_prop(32,prop,XA_ATOM,&a,1);
}

int main(int argc, char **argv)
{
   char *window_name = "xkbd";

   char *icon_name = "xkbd";

   XSizeHints size_hints;
   XWMHints *wm_hints;

   char *display_name = (char *)getenv("DISPLAY");

   Xkbd *kb = NULL;

//   char *wm_name;
//   int wm_type = WM_UNKNOWN;

   char *geometry = NULL;
   int xret=0, yret=0, wret=0, hret=0;
   char *conf_file = NULL;
   char *font_name = NULL;
   int cmd_xft_selected = 0; /* ugly ! */
   int embed = 0;
   int dock = 0;

   XEvent ev;
   int  xkbEventType = Expose; // any used berfore

   int i;
   char userconffile[256];
   FILE *fp;
   KeySym mode_switch_ksym;

   char **exec_cmd = argv;

   for (i=1; argv[i]; i++) {
      char *arg = argv[i];
      if (*arg=='-') {
	 switch (arg[1]) {
	    case 'd' : /* display */
	       display_name = argv[++i];
	       break;
	    case 'g' :
	       geometry = argv[++i];
	       break;
	    case 'f':
	       font_name = argv[++i];
#ifdef USE_XFT
	       cmd_xft_selected = 1;
#endif
	       break;
	    case 'k' :
	       conf_file = argv[++i];
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
	    case 'D' :
	       dock = atoi(argv[++i]);
	       break;
	    case 'X' :
#ifndef MINIMAL
	       Xkb_sync = 1;
#endif
	       break;
	    case 'e' :
	       exec_cmd = &argv[++i];
	       goto stop_argv;
	    case 'l' :
	       no_lock = 1;
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
stop_argv:

#ifndef MINIMAL
   if (Xkb_sync) {
	int xkbError, reason_rtrn, mjr = XkbMajorVersion, mnr = XkbMinorVersion;

	display = XkbOpenDisplay(display_name, &xkbEventType, &xkbError, &mjr, &mnr, &reason_rtrn);
	if (!display) goto no_dpy;
	XkbSelectEvents(display,XkbUseCoreKbd,XkbAllEventsMask,XkbStateNotifyMask|XkbNewKeyboardNotifyMask);
	XkbSelectEventDetails(display,XkbUseCoreKbd,XkbStateNotifyMask,XkbAllStateComponentsMask,XkbModifierStateMask|XkbModifierLatchMask|XkbModifierLockMask|XkbModifierBaseMask);
   } else
#endif
   {
	display = XOpenDisplay(display_name);
	if (!display) goto no_dpy;
   }
   screen_num = DefaultScreen(display);
   rootWin = RootWindow(display, screen_num);
/*
    // if you know where & how to use relative root window
   Window rootWin0;
   int sx,sy;
   unsigned int screen_width, screen_height, sbord. sdeph;
   XGetGeometry(display, rootWin, &rootWin0, &sx, &sy, &screen_width, &screen_height, &sbord, &sdepth);
*/
   unsigned int screen_width = DisplayWidth(display, screen_num);
   unsigned int screen_height = DisplayHeight(display, screen_num);

      Atom wm_protocols[]={
	 XInternAtom(display, "WM_DELETE_WINDOW",False),
	 XInternAtom(display, "WM_PROTOCOLS",False),
	 XInternAtom(display, "WM_NORMAL_HINTS", False),
      };

    mwm_atom = XInternAtom(display, "_MOTIF_WM_HINTS",False);

      /* HACK to get libvirtkeys to work without mode_switch */
	/* ??? 2delete? */
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

/*
      wm_name = get_current_window_manager_name ();

      if (wm_name)
	{
	  wm_type = WM_EHWM_UNKNOWN;
	  if (!strcmp(wm_name, "metacity"))
	    wm_type = WM_METACITY;
	  else if (!strcmp(wm_name, "matchbox"))
	    {

	      wm_type = WM_MATCHBOX;
	    }
	}
*/

      wret = screen_width;
      hret = screen_height/4;
      xret = 0;
      yret = screen_height - hret;
      if (geometry) {
	int flags = XParseGeometry(geometry, &xret, &yret, &wret, &hret );
	if( flags & XNegative ) xret += screen_width - wret;
	if( flags & YNegative ) yret += screen_height - hret;
      }

      win = XCreateSimpleWindow(display, rootWin, xret, yret, wret, hret, 0,
				BlackPixel(display, screen_num),
				WhitePixel(display, screen_num));

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

      _reset(0);
      signal(SIGTERM, _reset);

      kb = xkbd_realize(display, win, conf_file, font_name, 0, 0,
			wret, hret, cmd_xft_selected);
      if (wret != xkbd_get_width(kb) || hret != xkbd_get_height(kb)) {
	yret += hret - xkbd_get_height(kb);
        XMoveWindow(display,win,xret,yret);
        XResizeWindow(display, win, wret = xkbd_get_width(kb), hret = xkbd_get_height(kb));
      }
//      if (xret || yret)
//	 XMoveWindow(display,win,xret,yret);
      size_hints.flags = PPosition | PSize | PMinSize;
      size_hints.x = 0;
      size_hints.y = 0;
      size_hints.width      =  wret;
      size_hints.height     =  hret;
      size_hints.min_width  =  wret;
      size_hints.min_height =  hret;
      XSetStandardProperties(display, win, window_name,
			     icon_name, None,
			     argv, argc, &size_hints);
      XStoreName(display, win, window_name);
      XSetIconName(display, win, icon_name);
      wm_hints = XAllocWMHints();
      wm_hints->input = False;
      wm_hints->flags = InputHint;
      if (dock & 1) {
	wm_hints->flags |= IconWindowHint | WindowGroupHint | StateHint;
	wm_hints->initial_state = WithdrawnState;
	wm_hints->icon_window = wm_hints->window_group = win;
      }
      XSetWMHints(display, win, wm_hints );
      XSetWMProtocols(display, win, wm_protocols, sizeof(wm_protocols) /
		      sizeof(Atom));
		      
      long prop[12] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
      XChangeProperty(display,win, mwm_atom, mwm_atom,32,PropModeReplace,(unsigned char *)&prop,5);
      if(dock & 4)
	_propAtom32("_NET_WM_WINDOW_TYPE","_NET_WM_WINDOW_TYPE_DOCK");
      if (dock & 8)
	_propAtom32("_NET_WM_WINDOW_TYPE","_NET_WM_WINDOW_TYPE_TOOLBAR");
      if (dock & 16) 
	_propAtom32("_NET_WM_STATE","_NET_WM_STATE_STICKY");
//      _propAtom32("_NET_WM_STATE","_NET_WM_STATE_ABOVE");
//      _propAtom32("_NET_WM_STATE","_NET_WM_STATE_FOCUSED");
//      Atom version = 4;
//      _prop(32,"XdndAware",XA_ATOM,&version,1);
      if (dock & 2) {
        prop[0] = 0;
	prop[3] = screen_height - yret; // hret unless shifted geometry
	_prop(32,"_NET_WM_STRUT",XA_CARDINAL,&prop,4);
	prop[10] = xret; prop[11] = xret + wret - 1;
//	prop[11] = screen_width - 1;
	_prop(32,"_NET_WM_STRUT_PARTIAL",XA_CARDINAL,&prop,12);
      }

      XSelectInput(display, win,
		   ExposureMask |
//#ifndef USE_XI
		   ButtonPressMask |
		   ButtonReleaseMask |
		    // may be lost "release" on libinput
		    // probably need all motions or nothing
//		   Button1MotionMask |
//#endif
		   StructureNotifyMask |
		   VisibilityChangeMask);

      if (embed) {
	 fprintf(stdout, "%li\n", win);
	 fclose(stdout);
      } else {
	 XMapWindow(display, win);
	 XSync(display, False);
      }
      signal(SIGUSR1, handle_sig); /* for extenal mapping / unmapping */

#ifdef USE_XI
      int xiopcode, xievent, xierror;
      int ximajor = 2, ximinor = 2;
      if(XQueryExtension(display, "XInputExtension", &xiopcode, &xievent, &xierror) &&
		XIQueryVersion(display, &ximajor, &ximinor) != BadRequest) {

	XIEventMask mask = { .deviceid = XIAllDevices, .mask_len = XIMaskLen(XI_TouchEnd) };
	mask.mask = (unsigned char*)calloc(3, sizeof(char));

	XISetMask(mask.mask, XI_ButtonPress);
	XISetMask(mask.mask, XI_ButtonRelease);

//	XISetMask(mask.mask, XI_Motion);

	XISetMask(mask.mask, XI_TouchBegin);
	XISetMask(mask.mask, XI_TouchUpdate);
	XISetMask(mask.mask, XI_TouchEnd);
	XISelectEvents(display, win, &mask, 1);
	free(mask.mask);
      }

#endif


      while (1)
      {
	    int type = 0;
	    XNextEvent(display, &ev);
	    switch (ev.type) {
	    case ButtonRelease: type++;
	    case MotionNotify: type++;
	    case ButtonPress:
		xkbd_process(kb, type, ev.xmotion.x, ev.xmotion.y, 0, ev.xmotion.time);
		break;
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
#ifdef USE_XI
		case GenericEvent: if (ev.xcookie.extension == xiopcode 
//		    && ev.xgeneric.extension == 131
		    && XGetEventData(display, &ev.xcookie)
		    ) {
#undef e
#define e ((XIDeviceEvent*)ev.xcookie.data)
//			switch(e->evtype) {
			switch(ev.xcookie.evtype) {
			    case XI_TouchEnd: type++;
			    case XI_TouchUpdate: type++;
			    case XI_TouchBegin:
				xkbd_process(kb, type, round(e->event_x), round(e->event_y), e->detail, e->time);
				break;
			    case XI_ButtonRelease: type++;
			    case XI_Motion: type++;
			    case XI_ButtonPress:
				xkbd_process(kb, type, round(e->event_x), round(e->event_y), e->detail, e->time);
				break;
			}
			XFreeEventData(display, &ev.xcookie);
		}
		break;
#endif
#ifndef MINIMAL
		default: if (ev.type == xkbEventType) {
#undef e
#define e ((XkbEvent)ev)
			switch (e.any.xkb_type) {
			    case XkbStateNotify:
#undef e
#define e (((XkbEvent)ev).state)
				if (xkbd_sync_state(kb,e.mods,e.locked_mods,e.group))
					xkbd_repaint(kb);
				break;
			    case XkbNewKeyboardNotify:
#undef e
#define e (((XkbEvent)ev).new_kbd)
				// Xkbd send false notify, so we must compare maps
				if (e.min_key_code != e.old_min_key_code || e.max_key_code != e.old_max_key_code || e.device != e.old_device
				    || kb_load_keymap(display)
				    ) {
					XCloseDisplay(display);
					xkbd_destroy(kb);
					execvp(exec_cmd[0], exec_cmd);
				}
				break;
			}
		}
#endif
	    }
	    while (xkbd_process_repeats(kb) && !XPending(display))
		usleep(10000L); /* sleep for a 10th of a second */
      }
no_dpy:
	fprintf(stderr, "%s: cannot connect to X server '%s'\n", argv[0], display_name);
	exit(1);
}


