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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/shape.h>
#include <X11/Xresource.h>

#ifdef USE_XI
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#endif

#ifdef USE_XR
#include <X11/extensions/Xrandr.h>
#endif

#include "../config.h"

#include "libXkbd.h"

#define DEBUG 1

#define WIN_NORMAL  1
#define WIN_OVERIDE 2

#define WIN_OVERIDE_AWAYS_TOP 0
#define WIN_OVERIDE_NOT_AWAYS_TOP 1

char *IAM = "xkbd";

Display* display; /* ack globals due to sighandlers - another way ? */
Window   win;
Window rootWin;
Atom mwm_atom;
int screen;
XWindowAttributes wa0;

Xkbd *kb = NULL;
char **exec_cmd;

int Xkb_sync = 0;
int no_lock = 0;
#ifdef CACHE_PIX
int cache_pix = 1;
#endif

// dpi
unsigned long scr_width;
unsigned long scr_height;
unsigned long scr_mwidth;
unsigned long scr_mheight;

// IBM standard 500x200mm, ~22x6 keys? or key 20x20mm
// int max_width = 440, max_height=120;

enum {
  WM_UNKNOWN,
  WM_EHWM_UNKNOWN,
  WM_METACITY,
  WM_MATCHBOX,
};

static int xerrh(Display *dsp, XErrorEvent *ev) {
	char buf[256];

	fprintf(stderr,"%s: X error %i ",IAM,ev->error_code);
	if (!XFlush(dsp)) {
		fprintf(stderr,"fatal\n");
		exit(1);
	}
	buf[0]=0;
	XGetErrorText(dsp,ev->error_code,(char*)&buf,sizeof(buf));
	fprintf(stderr,"%s %s\n",XDisplayString(dsp),buf);
	return 0;
}

/*
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

  XFree(xwindow); // ?
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
*/

static void handle_sig(int sig)
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

void restart(){
	// display will be closed anymore, but prefer to ignore exec failures
//	XCloseDisplay(display);
//	xkbd_destroy(kb);
	execvp(exec_cmd[0], exec_cmd);
//	exit(1);
}

static void _restart(int sig){
	restart();
}

static int stopped = 0;
static void _reset(int sig){
	if (!stopped) {
		stopped = sig;
		XkbLockModifiers(display,XkbUseCoreKbd,0xffff,0);
		XkbLatchModifiers(display,XkbUseCoreKbd,0xffff,0);
		XkbLockGroup(display,XkbUseCoreKbd,0);
	}
	if (stopped) exit(1);
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

void _prop(int i, char *prop, Atom type, void *data, int n){
	XChangeProperty(display,win,prop?XInternAtom(display,prop,False):mwm_atom,type,i,PropModeReplace,(unsigned char *)data,n); 
}

void _propAtom32(char *prop, char *data){
	Atom a=XInternAtom(display,data,False);
	_prop(32,prop,XA_ATOM,&a,1);
}

int inbound(int x,int xin,int x1,int x2){
	return (xin>x1 && xin<x2)?xin:x;
}

int main(int argc, char **argv)
{
   char *window_name = IAM;
   char *icon_name = IAM;

   XSizeHints size_hints;
   XWMHints *wm_hints;
   
   char *display_name = NULL;

//   char *wm_name;
//   int wm_type = WM_UNKNOWN;

   static char *geometry = NULL;
   static char *output = NULL;
   int x=0, y=0, width=0, height=0;
   int top=0, left=0;
   int crts=0;
   static char *conf_file = NULL;
   static char *font_name = NULL;
   static char *font_name1 = NULL;
   int embed = 0;
   static unsigned int dock = 0;
   XrmDatabase xrm;
   Window win1;

   XEvent ev;
   int xkbEventType = 0;

   int i,j,w,h;
   char *s;
   char userconffile[256];
   FILE *fp;
   KeySym mode_switch_ksym;

   static struct resource {
	char param;
	char *name;
	int type;
	void *ptr;
	char *help;
   } __attribute__ ((__packed__)) resources[] = {
	{ 'g', "xkbd.geometry", 0, &geometry, "(default -0-0, left/top +0+0)\n\
     ( NOTE: The above will overide the configs font )" },
	{ 'f', "xkbd.font_name", 0, &font_name, "font" },
	{ '1', "xkbd.font_name1", 0, &font_name1, "font for 1-char" },
	{ 'k', "xkbd.conf_file", 0, &conf_file, "keyboard definition file\n\
                      other than" DEFAULTCONFIG },
	{ 'D', "xkbd.dock", 1, &dock, "Dock/options bitmask: 1=dock, 2=strut, 4=_NET_WM_WINDOW_TYPE_DOCK,\n\
      8=_NET_WM_WINDOW_TYPE_TOOLBAR, 16=_NET_WM_STATE_STICKY.\n\
      32=resize (slock), 64=strut horizontal, 128=_NET_WM_STATE_SKIP_TASKBAR\n\
      For OpenBox I use 178 = $[2+16+32+128]." },
#ifndef MINIMAL
	{ 'X', "xkbd.sync", 2, &Xkb_sync, "Xkb state interaction" },
#else
	{ 'X' },
#endif
	{ 'l', "xkbd.no_lock", 2, &no_lock, "disable modifiers lock" },
#ifdef USE_XR
	{ 'o', "xkbd.output", 0, &output, "xrandr output name" },
#else
	{ 'o' },
#endif
#ifdef CACHE_PIX
	{ 'C', "xkbd.cache", 1, &cache_pix, "pixmap cache 0=disable, 1=enable/default, 2=preload" },
#endif
	{ 0, NULL }
   };
   struct resource *res1;

   exec_cmd = argv;

   for (i=1; s=argv[i]; i++) {
      int res = -1;
      if (*s=='-') {
	 switch (s[1]) {
	    case 'd' :
		display_name = argv[++i];
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
	    case 'e' :
		exec_cmd = &argv[++i];
		goto stop_argv;
	    case 'v' :
		version();
		exit(0);
		break;
	    default:
		for (res=0; (res1=&resources[res])->param!=s[1]; res++) {
			if (!res1->param) {
   printf("Usage: %s <options>\n\
Options:\n\
  -d  <display>\n\
  -x  used for gtk embedding\n\
  -c  dock\n\
  -s  strut\n\
  -v  version\n\
  -e {<...>}   exec on restart (to end of line)\n\
  -h  this help\n", IAM);
				for (res=0; (res1=&resources[res])->param; res++) {
					char *t,*h=res1->help?:"";
					switch (res1->type) {
					case 0:t="string";break;
					case 1:t="int";break;
					case 2:printf("  -%c [<%s: 1>]  %s\n",res1->param, res1->name, h);continue;
					}
					printf("  -%c <%s>  (%s) %s\n",res1->param, res1->name, t, h);
				}
				exit(0);
			}
		};
		res1->name = ""; // top priority
		switch (res1->type) {
		    case 0: 
			*(char **)res1->ptr = *argv[++i]?argv[i]:NULL;
			break;
		    case 1:
			*(int *)res1->ptr = atoi(argv[++i]);
			break;
		    case 2:
			*(int *)res1->ptr = 1;
			break;
		}
		break;
	 }
      }
   }
stop_argv:
//   signal(SIGHUP, _restart); // only 1

   display = XOpenDisplay(display_name);
   if (!display) goto no_dpy;
   screen = DefaultScreen(display);
   rootWin = RootWindow(display, screen);
   scr_mwidth=DisplayWidthMM(display, screen);
   scr_mheight=DisplayHeightMM(display, screen);
   XGetWindowAttributes(display,rootWin,&wa0);

   int X1=wa0.x,Y1=wa0.y,X2=wa0.x+wa0.width-1,Y2=wa0.y+wa0.height-1;
   char *rs;

   if ((rs = XResourceManagerString(display)) && (xrm = XrmGetStringDatabase(rs))) {
    for (i=0; s=(res1=&resources[i])->name; i++) {
	char *type = NULL;
	XrmValue val;
	int n;
	val.addr = NULL;

	if (*s && XrmGetResource(xrm,s,NULL,&type,&val) && val.addr) {
		switch (res1->type) {
		case 0:
			*(char **)(res1->ptr) = NULL;
			n = strlen((char *)val.addr)+1;
			if (n>1) memcpy(*(char **)(res1->ptr) = malloc(n), val.addr, n);
			break;
		case 1:
		case 2:
			*(int *)res1->ptr=atoi((char *)val.addr);
			break;
		}
	}
    }
    XrmDestroyDatabase(xrm);
  }
  XFree(rs);

#ifdef USE_XR
	// find actual output;
	// if no geometry, try to avoid strut in the middle of overlapped outputs (-> top/left).
	int xrevent, xrerror, xrr;
	if ((xrr = XRRQueryExtension(display, &xrevent, &xrerror))) {
		int found=0, repeat=0;
		XRRScreenResources *xrrr = XRRGetScreenResources(display,rootWin);
re_crts:
		repeat++;
		crts=0;
		for (i = 0; i < xrrr->noutput; i++) {
			XRROutputInfo *oinf = XRRGetOutputInfo(display, xrrr, xrrr->outputs[i]);
			if (oinf && oinf->crtc && oinf->connection == RR_Connected) {
				XRRCrtcInfo *cinf = XRRGetCrtcInfo (display, xrrr, oinf->crtc);
				int x1=cinf->x;
				int y1=cinf->y;
				int x2=x1+cinf->width-1;
				int y2=y1+cinf->height-1;
				XRRFreeCrtcInfo(cinf);
				crts++;
				if (found) {
					if (geometry) continue;
					if (dock & 2 && y2>Y2 && x2>X1 && x1<=X1) top=1;
					else if (dock & 64 && x2>X2 && y2>Y1 && y1<=X1) left=1;
					continue;
				} else if (repeat==1 && (output?
				    strcmp(oinf->name,output):(
					!strncasecmp(oinf->name,"HDMI",4)||
					!strncasecmp(oinf->name,"DP",2)||
					!strncasecmp(oinf->name,"DVI",3)
				    ))
				    ) continue;
				found++;
				X1=max(X1,x1);
				Y1=max(Y1,y1);
				X2=min(X2,x2);
				Y2=min(Y2,y2);
				scr_mwidth=oinf->mm_width;
				scr_mheight=oinf->mm_height;
				if (crts>1 && !geometry && dock & (64|2)) goto re_crts;
			}
			XRRFreeOutputInfo(oinf);
		}
		if (!found) {
			if (output) fprintf(stderr,"Output '%s' not found, ignoring\n",output);
			if(crts) goto re_crts;
		}
		XRRFreeScreenResources(xrrr);
	}
#endif

      Atom wm_protocols[]={
	 XInternAtom(display, "WM_DELETE_WINDOW",False),
	 XInternAtom(display, "WM_PROTOCOLS",False),
	 XInternAtom(display, "WM_NORMAL_HINTS", False),
      };

    mwm_atom = XInternAtom(display, "_MOTIF_WM_HINTS",False);

      /* HACK to get libvirtkeys to work without mode_switch */
	/* ??? 2delete? */
/*
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

      scr_width=X2-X1+1;
      scr_height=Y2-Y1+1;
      if (geometry) {
        // default = +0-0 = xH+0-0 = WxH+0-0 (sometimes +0+0)
	int flags = XParseGeometry(geometry, &x, &y, &width, &height);
	left = !(flags & XNegative);
	top = !(flags & YNegative);
      }

      x += X1;
      y += Y1;
      // if unknown - try relevant temporary size = 500x200mm or 3x1
      w=width?:height?min(height*3,scr_width):scr_mwidth?500*scr_width/scr_mwidth:scr_width;
      h=height?:scr_mheight?200*scr_height/scr_mheight:(min(scr_height,w)/3);
//      w=width?:scr_width; h=height?:scr_height;
      if (!left) x += scr_width - w;
      if (!top) y += scr_height - h;

      win = XCreateSimpleWindow(display, rootWin, x, y, w, h,
	0, BlackPixel(display, screen), WhitePixel(display, screen));



      /* check for user selected keyboard conf file */


      if (conf_file == NULL)
	{
	  strcpy(userconffile,getenv("HOME"));
	  strcat(userconffile, "/.");
	  strcat(userconffile, IAM);

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

      kb = xkbd_realize(display, win, conf_file, font_name, font_name1, 0, 0,
			width, height);
      i=xkbd_get_width(kb);
      j=xkbd_get_height(kb);
      if (width != i || height != j) {
	if (!left) x += w - i;
	if (!top) y += h - j;
	XMoveResizeWindow(display,win,x,y,width=i,height=j);
      }

      size_hints.flags = PPosition | PSize | PMinSize;
      size_hints.x = 0;
      size_hints.y = 0;
      size_hints.width      =  width;
      size_hints.height     =  height;
      size_hints.min_width  =  width;
      size_hints.min_height =  height;
      XSetStandardProperties(display, win, window_name,
			     icon_name, None,
			     argv, argc, &size_hints);
      wm_hints = XAllocWMHints();
      wm_hints->input = False;
      wm_hints->flags = InputHint;
      if (dock & 1) {
	wm_hints->flags |= IconWindowHint | WindowGroupHint | StateHint;
	wm_hints->initial_state = WithdrawnState;
	wm_hints->icon_window = wm_hints->window_group = win;
      }
      XSetWMHints(display, win, wm_hints);
      XFree(wm_hints);
      XSetWMProtocols(display, win, wm_protocols, sizeof(wm_protocols) /
		      sizeof(Atom));
		      
      long prop[12] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
      XChangeProperty(display,win, mwm_atom, mwm_atom,32,PropModeReplace,(unsigned char *)&prop,5);
      prop[0] = 0;
      if(dock & 4)
	_propAtom32("_NET_WM_WINDOW_TYPE","_NET_WM_WINDOW_TYPE_DOCK");
      if (dock & 8)
	_propAtom32("_NET_WM_WINDOW_TYPE","_NET_WM_WINDOW_TYPE_TOOLBAR");
      if (dock & 16)
	_propAtom32("_NET_WM_STATE","_NET_WM_STATE_STICKY");
      if (dock & 128)
	_propAtom32("_NET_WM_STATE","_NET_WM_STATE_SKIP_TASKBAR");
//      _propAtom32("_NET_WM_STATE","_NET_WM_STATE_ABOVE");
//      _propAtom32("_NET_WM_STATE","_NET_WM_STATE_FOCUSED");
//      Atom version = 4;
//      _prop(32,"XdndAware",XA_ATOM,&version,1);


      XSelectInput(display, win,
		   ExposureMask |
//#ifndef USE_XI
		   ButtonPressMask |
		   ButtonReleaseMask |
		    // may be lost "release" on libinput
		    // probably need all motions or nothing
//		   Button1MotionMask |
		   ButtonMotionMask |
//#endif
		   StructureNotifyMask |
		   VisibilityChangeMask);

      if (embed) {
	 fprintf(stdout, "%li\n", win);
	 fclose(stdout);
      } else {
	XMapWindow(display, win);
      }
      signal(SIGUSR1, handle_sig); /* for extenal mapping / unmapping */

#ifdef USE_XR
      if (xrr) {
	XRRSelectInput(display, win, RRScreenChangeNotifyMask);
      }
#endif

#ifndef MINIMAL
	// not found how to get keymap change on XInput, so keep Xkb events
	int xkbError, reason_rtrn, xkbmjr = XkbMajorVersion, xkbmnr = XkbMinorVersion, xkbop;
	if (XkbQueryExtension(display,&xkbop,&xkbEventType,&xkbError,&xkbmjr,&xkbmnr)) {
		if (Xkb_sync) {
			XkbSelectEvents(display,XkbUseCoreKbd,XkbAllEventsMask,XkbNewKeyboardNotifyMask|XkbStateNotifyMask);
			XkbSelectEventDetails(display,XkbUseCoreKbd,XkbStateNotifyMask,XkbAllStateComponentsMask,XkbModifierStateMask|XkbModifierLatchMask|XkbModifierLockMask|XkbModifierBaseMask);
		} else {
			XkbSelectEvents(display,XkbUseCoreKbd,XkbAllEventsMask,XkbNewKeyboardNotifyMask);
		}
	} else xkbEventType = 0;
#endif

#ifdef USE_XI
      int xiopcode, xievent = 0, xierror, xi = 0;
      int ximajor = 2, ximinor = 2;
      if(XQueryExtension(display, "XInputExtension", &xiopcode, &xievent, &xierror) &&
		XIQueryVersion(display, &ximajor, &ximinor) != BadRequest) {

	unsigned char mask_[3] = {0, 0, 0};
	XIEventMask mask = { .deviceid = XIAllDevices, .mask_len = XIMaskLen(XI_TouchEnd), .mask = (unsigned char *)&mask_ };

	// keep "button" events in standard events while
//	XISetMask(mask.mask, XI_ButtonPress);
//	XISetMask(mask.mask, XI_ButtonRelease);

//	XISetMask(mask.mask, XI_Motion);

	XISetMask(mask.mask, XI_TouchBegin);
	XISetMask(mask.mask, XI_TouchUpdate);
	XISetMask(mask.mask, XI_TouchEnd);
	XISelectEvents(display, win, &mask, 1);
	xi = 1;
      }
#endif
      XSetErrorHandler(xerrh);
      XSync(display, False);

      while (1)
      {
	    int type = 0;
	    XNextEvent(display, &ev);
	    switch (ev.type) {
#ifdef USE_XI
		case GenericEvent: if (xi && ev.xcookie.extension == xiopcode
//		    && ev.xgeneric.extension == 131
		    && XGetEventData(display, &ev.xcookie)
		    ) {
#undef e
#define e ((XIDeviceEvent*)ev.xcookie.data)
//			switch(e->evtype) {
			switch(ev.xcookie.evtype) {
			    //case XI_ButtonRelease:
			    case XI_TouchEnd: type++;
			    //case XI_Motion:
			    case XI_TouchUpdate: type++;
			    //case XI_ButtonPress:
			    case XI_TouchBegin:
				xkbd_process(kb, type, e->event_x + .5, e->event_y + .5, e->detail, e->sourceid, e->time);
			}
			XFreeEventData(display, &ev.xcookie);
		}
		break;
#endif
	    case ButtonRelease: type++;
	    case MotionNotify: type++;
	    case ButtonPress:
		xkbd_process(kb, type, ev.xmotion.x, ev.xmotion.y, 0, 0, ev.xmotion.time);
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
	    case MapNotify:
		XTranslateCoordinates(display,win,rootWin,0,0,&x,&y,&win1);
		if (dock & 2) {
			if (top) {
				prop[2] = y + height;
				prop[8] = x;
				prop[9] = x + width - 1;
			} else {
				prop[3] = wa0.height - y;
				prop[10] = x;
				prop[11] = x + width - 1;
			}
		}
		if (dock & 64) {
			if (left) {
				prop[0] = x + width;
				prop[4] = y;
				prop[5] = y + height - 1;
			} else {
				prop[1] = wa0.width - x;
				prop[6] = y;
				prop[7] = y + height - 1;
			}
		}
		if (dock & (2|64)) {
//			for(i=0;i<12;i++) fprintf(stderr," %li",prop[i]);fprintf(stderr," crts=%i\n",crts);
			//0: left, right, top, bottom,
			// don't strut global on xrandr multihead
			if (crts<2) _prop(32,"_NET_WM_STRUT",XA_CARDINAL,&prop,4);
			//4: left_start_y, left_end_y, right_start_y, right_end_y,
			//8: top_start_x, top_end_x, bottom_start_x, bottom_end_x
			_prop(32,"_NET_WM_STRUT_PARTIAL",XA_CARDINAL,&prop,12);
		}
		break;
	    case VisibilityNotify: if (dock & 32) {
		Window rw, pw, *wins;
		unsigned int nw;
		XWindowAttributes wa;
		static int lock_cnt = 0;

		if (ev.xvisibility.state!=VisibilityFullyObscured ||
			display!=ev.xvisibility.display ||
			win!=ev.xvisibility.window ||
			!XQueryTree(display, rootWin, &rw, &pw, &wins, &nw)
			) break;
		while (nw--) {
			// BUG! can fail on OpenBox menu (async?)
			if (XGetWindowAttributes(display, wins[nw],&wa) &&
				wa.screen == wa0.screen &&
				wa.x<=x && wa.y<y && wa.width>=width && wa.height>height &&
				wa.x+wa.width>x && wa.y+wa.height>y
				) XResizeWindow(display, wins[nw], wa.width, y-wa.y);
		}
		XFree(wins);
		// first lock: fork (to realize init-lock safe wait)
		if (!lock_cnt++ && fork()) exit(0);
	    }
	    break;
	    case 0: break;
	    default:
#ifndef MINIMAL
		if (ev.type == xkbEventType) {
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
				    ) restart();
				break;
			}
			break;
		}
#endif
#ifdef USE_XR
		if (xrr && ev.type == xrevent + RRScreenChangeNotify) restart();
#endif
	    }
	    while (xkbd_process_repeats(kb) && !XPending(display))
		usleep(10000L); /* sleep for a 10th of a second */
      }
no_dpy:
	fprintf(stderr, "%s: cannot connect to X server\n", argv[0]);
	exit(1);
}
