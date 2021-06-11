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

#include <X11/Xproto.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/shape.h>
#include <X11/Xresource.h>

#ifdef USE_XI
//#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#endif

#ifdef USE_XR
#include <X11/extensions/Xrandr.h>
#endif

#include "../config.h"

#include "structs.h"
#include "kb.h"

#define DEBUG 1

#define WIN_NORMAL  1
#define WIN_OVERIDE 2

#define WIN_OVERIDE_AWAYS_TOP 0
#define WIN_OVERIDE_NOT_AWAYS_TOP 1

#define SIG_HIDE SIGUSR2
#define SIG_SHOW SIGHUP


#define IAM "xkbd"
char *iam = IAM;

Display* display; /* ack globals due to sighandlers - another way ? */
Window   win;
Window rootWin;
int screen;
XWindowAttributes wa0;
char *display_name = NULL;

CARD32 prop[12] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int crts=0, X1,Y1,X2,Y2, top=0, left=0;
//unsigned int dock = 1974;
unsigned int dock = 4018;

#define UNSIZE 1
int resized = 0;
int hide_enabled = 0;

keyboard *kb = NULL;
button *active_but = NULL;

char **exec_cmd;

int Xkb_sync = 2;
int no_lock = 0;
int swipe_fingers = 2;
#ifdef CACHE_PIX
int cache_pix = 1;
#endif

// dpi
unsigned long scr_width;
unsigned long scr_height;
unsigned long scr_mwidth;
unsigned long scr_mheight;
SubpixelOrder subpixel_order;

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

	fprintf(stderr,"%s: X error %i ",iam,ev->error_code);
	if (!XFlush(dsp)) {
		fprintf(stderr,"fatal\n");
		exit(1);
	}
	buf[0]=0;
	XGetErrorText(dsp,ev->error_code,(char*)&buf,sizeof(buf));
	fprintf(stderr,"%s %s\n",XDisplayString(dsp),buf);
	return 0;
}

static Atom _atom(char *a){
	return XInternAtom(display, a, False);
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

static void restart(){
	// display will be closed anymore, but prefer to ignore exec failures
	XFlush(display);
	XSync(display,False);
//	XCloseDisplay(display);
//	xkbd_destroy(kb);
	execvp(exec_cmd[0], exec_cmd);
//	exit(1);
}

static void _restart(int sig){
	restart();
}

static void _reset(){
	// some dangerous flags combinations are buggy. reset
	XkbLockModifiers(display,XkbUseCoreKbd,0xffff,0);
	XkbLatchModifiers(display,XkbUseCoreKbd,0xffff,0);
//	XkbLockGroup(display,XkbUseCoreKbd,0);
	//XFlush(display);
}

static void _signals(void *f){
	signal(SIGINT,f);
	signal(SIGABRT,f);
	signal(SIGQUIT,f);
}

static void signal_exit(int sig){
	_signals(SIG_DFL);
	if (display && (display = XOpenDisplay(display_name))) {
		_reset();
		XFlush(display);
		XCloseDisplay(display);
	}
	exit(1);
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

static void _prop(int i, Atom prop, Atom type, void *data, int n, int mode){
	XChangeProperty(display,win,prop,type,i,mode,(unsigned char *)data,n); 
}

static void _propAtom32(Atom prop, char *data){
	Atom a=_atom(data);
	_prop(32,prop,XA_ATOM,&a,1,PropModeAppend);
}

static void _propAtom32Rep(Atom prop, char *data){
	Atom a=_atom(data);
	_prop(32,prop,XA_ATOM,&a,1,PropModeAppend);
}

static void _propCard32(Atom prop, void *data, int n){
	_prop(32,prop,XA_CARDINAL,data,n,PropModeReplace);
}

static int inbound(int x,int xin,int x1,int x2){
	return (xin>x1 && xin<x2)?xin:x;
}

static void reset2(){
	XkbStateRec s;
	int g = kb->group;
	XkbGetState(display,XkbUseCoreKbd,&s);
	kb_sync_state(kb,s.mods,s.locked_mods,s.group);
	if (g==s.group) kb_repaint(kb);
}

static void _move(int x, int y, int width, int height){
	XSizeHints size_hints = {
		.flags = PPosition | PMinSize | PMaxSize,
		.x = x,
		.y = y,
		.min_width = width,
		.max_width = width,
		.min_height = height,
		.max_height = height,
	};
	XSetNormalHints(display,win,&size_hints);
	XSetWMNormalHints(display,win,&size_hints);
	XMoveResizeWindow(display,win,x,y,width,height);
}

static void transCoord(){
	Window win1;
	kb->X = kb->Y = 0;
	XFlush(display);
	XTranslateCoordinates(display,win,rootWin,0,0,&kb->X,&kb->Y,&win1);
}

int sig[8] = {};
Atom aStrut,aStrutPartial;
static void setSize(int x, int y, int width, int height, int resize){
	resize*=UNSIZE;
	if (dock & 2) {
		if (top) {
			if (resize) {
				y = wa0.y;
				height = resize;
			}
			prop[2] = y + height; // +Y1
			prop[8] = x;
			prop[9] = x + width - 1;
			sig[5] = SIG_HIDE;
			sig[4] = SIG_SHOW;
		} else {
			if (resize) {
				//y = y + height - resize;
				y = wa0.height - resize;
				height = resize;
			}
			prop[3] = Y2 - y + 1;
			prop[10] = x;
			prop[11] = x + width - 1;
			sig[5] = SIG_SHOW;
			sig[4] = SIG_HIDE;
		}
	}
	if (dock & 64) {
		if (left) {
			if (resize) {
				x = wa0.x;
				width = resize;
			}
			prop[0] = x + width; // +X1
			prop[4] = y;
			prop[5] = y + height - 1;
			sig[7] = SIG_HIDE;
			sig[6] = SIG_SHOW;
		} else {
			if (resize) {
				//x = x + width - resize;
				x = wa0.width - resize;
				width = resize;
			}
			prop[1] = X2 - x + 1;
			prop[6] = y;
			prop[7] = y + height - 1;
			sig[7] = SIG_SHOW;
			sig[6] = SIG_HIDE;
		}
	}
	if (dock & (2|64)) {
//		for(i=0;i<12;i++) fprintf(stderr," %li",prop[i]);fprintf(stderr," crts=%i\n",crts);
		//0: left, right, top, bottom,
		// don't strut global on xrandr multihead
		if (crts<2) _propCard32(aStrut,&prop,4);
		//4: left_start_y, left_end_y, right_start_y, right_end_y,
		//8: top_start_x, top_end_x, bottom_start_x, bottom_end_x
		_propCard32(aStrutPartial,&prop,12);
	}
	if (resized != resize) _move(x,y,width,height);
	resized = resize;
	transCoord();
}

int x=0, y=0, width=0, height=0;
static void _hide(int sig) {
	if (sig && hide_enabled) {
		setSize(x,y,width,height,sig == SIG_HIDE);
		XFlush(display);
	}
}

static short remapped = 0;
Atom atype, aActions, aDock;
static int unmapWin(){
		if (!(dock & 2048) || remapped) return 0;
		// unmap (remap) with DOCK.
		// Let WM apply offsets first to respect other docks/panels
		remapped = 1;
		XUnmapWindow(display, win);
		XFlush(display);
		XDeleteProperty(display,win,aActions);
		XDeleteProperty(display,win,aStrut);
		XDeleteProperty(display,win,aStrutPartial);
		_prop(32,atype,XA_ATOM,&aDock,1,PropModeReplace);
		XFlush(display);
		XSync(display,False);
		return 1;
}

#ifdef USE_XR
struct _Geometry {
	void *next;
	unsigned long w,h,mw,mh;
	keyboard *kb;
	SubpixelOrder subpixel_order;
} *geo0 = NULL, *geo = NULL;
#endif
static void unmapOrRestart(){
	if (!(dock & 2048)) restart();
	remapped = 0;
	unmapWin();
	x = y = width = height = 0;
#ifdef USE_XR
	if (!geo) {
		geo = malloc(sizeof(*geo));
		geo->w = scr_width;
		geo->h = scr_height;
		geo->mw = scr_mwidth;
		geo->mh = scr_mheight;
		geo->kb = kb;
		geo->subpixel_order = subpixel_order;
		geo->next = geo0;
		geo0 = geo;
	}
#endif
}

static int rootChanged(XWindowAttributes *wa){
	XGetWindowAttributes(display,rootWin,wa);
	if (wa0.x!=wa->x || wa0.y!=wa->y || wa0.width!=wa->width || wa0.height!=wa->height) {
		unmapOrRestart();
		return 1;
	}
	wa0 = *wa;
	return 0;
}

#ifdef USE_XI
int use_pressure = 0, z_number = 2;
unsigned short z_byte = 0;
unsigned char z_mask = 7;
// value > 0 -> exists
#define Z_ADD -1.1
double z_min = Z_ADD;
static short abs3(int devid){
	static int devid_abs_chk = 0;
	static int evtype_abs_chk = 0;
	static short ret = 0;
	if (devid == devid_abs_chk) goto ret1;
	if (use_pressure == -1 || use_pressure == devid) return 1;
	if (devid < 2) return 0;
	ret = 0;
	int ndevs2,i,l,nabs;
	unsigned char *label;
	XIAnyClassInfo *cl3 = NULL, *cl;
	XIDeviceInfo *d2 = XIQueryDevice(display, devid_abs_chk = devid, &ndevs2);
	if (!ndevs2 || devid != d2->deviceid) goto ret;
	for (i=0; i<d2->num_classes; i++) {
		cl = d2->classes[i];
		if (cl->type != XIValuatorClass) continue;
#undef e
#define e ((XIValuatorClassInfo*)cl)
		if (e->mode != Absolute) continue;
#if 0
		if (e->number == 2) {
			fprintf(stderr,"input %i '%s' using pressure\n",devid,d2->name);
			ret = 1;
			break;
		}
#else
		label = XGetAtomName(display,e->label);
		l = strlen(label);
		if (   (l > 8  && !strcmp(&label[l-8],"Pressure"))
		    || (l > 2  && !strcmp(&label[l-2]," Z"))
			    ) {
				ret = 1;
				break;
			}
			XFree(label);
			if (e->number == 2) cl3 = cl;
#endif
	}
	if (!ret && cl3 && use_pressure == devid) {
		label = XGetAtomName(display,e->label);
		cl = cl3;
		ret = 1;
		fprintf(stderr,"forced unknown label: ");
	}
	if (ret) {
		z_number = e->number;
		fprintf(stderr,"input %i '%s' using pressure valuator %i '%s'\n",devid,d2->name,z_number,label);
		XFree(label);
		z_byte = z_number >> 3;
		z_mask = 1 << (e->number & 7);
		z_min = e->min + Z_ADD;
		ret = 1;
	}
ret:
	if (d2) XIFreeDeviceInfo(d2);
ret1:
	return ret;
}
#endif

int main(int argc, char **argv)
{
   char *window_name = iam;
   char *icon_name = iam;

//   char *wm_name;
//   int wm_type = WM_UNKNOWN;

   static char *geometry = NULL;
   static char *output = NULL;
   static int fake_touch = 0;
   static char *conf_file = NULL;
   static char *font_name = NULL;
   static char *font_name1 = NULL;
   int embed = 0;
   XrmDatabase xrm;
   Window win1;

   XEvent ev;
   int xkbEventType = 0;

//   long evmask;
   XSetWindowAttributes setWA;
#define evmask setWA.event_mask
   evmask = ExposureMask|StructureNotifyMask|VisibilityChangeMask;


   int i,j,w,h;
   char *s,*r;
   char userconffile[256];
   FILE *fp;
   KeySym mode_switch_ksym;
   XWindowAttributes wa;

   static struct resource {
	char param;
	char *name;
	char type,ch;
	void *ptr;
	char *help;
   } __attribute__ ((__packed__)) resources[] = {
	{ 'g', IAM ".geometry", 0, 0, &geometry, "(default -0-0, left/top +0+0)" },
	{ 'f', IAM ".font_name", 0, 0, &font_name, "force font\n\
	default (if not in file): " DEFAULT_FONT "" },
	{ '1', IAM ".font_name1", 0, 0, &font_name1, "force font for 1-char" },
	{ 'k', IAM ".conf_file", 0, 0, &conf_file, "keyboard definition file\n\
	other than from ~/." IAM " or " DEFAULTCONFIG },
	{ 'D', IAM ".dock", 1, 0, &dock, "Dock/options bitmask:\n\
	1=dock, 2=strut, 4=_NET_WM_WINDOW_TYPE_DOCK,\n\
	8=_NET_WM_WINDOW_TYPE_TOOLBAR, 16=_NET_WM_STATE_STICKY,\n\
	32=resize (slock), 64=strut horizontal, 128=_NET_WM_STATE_SKIP_TASKBAR,\n\
	256=_NET_WM_STATE_ABOVE + RaiseWindow(), 512=_NET_WM_DESKTOP=0xffffffff,\n\
	1024=_NET_WM_STATE_SKIP_PAGER,\n\
	2048=remap: pending _NET_WM_WINDOW_TYPE_DOCK"
#ifdef USE_XR
	" + cached Xrandr/rotation"
#endif
	",\n	4096=override_redirect"
	".\n		(_NET_WM_WINDOW_TYPE_DOCK is hardwired, but can hide\n\
		other toolbar/panel, so use pending hack where possible)\n\
	For OpenBox I use 4018 = $[2+16+32+128+256+512+1024+2048]," },
	{ 'i', IAM ".fake_touch", 2, 0, &fake_touch,"event type bitmask: "
#ifdef USE_XI
		"1=Xkb vs. XI2,\n\
	2-XIAllMasterDevices vs. XIAllDevices, "
#endif
		"4-disable motion events"
	},
	{ 'G', IAM ".gestures", 1, 0, &swipe_fingers, ""
#ifdef GESTURES_EMULATE
	"swipe fingers (0=disabled)"
#ifdef MULTITOUCH
	", 2=2-fingers"
#endif
	"\n	(_NET_WM_WINDOW_TYPE_DOCK required to hide/show)"
#endif
	},
	
	{ 'X', IAM ".sync", 2, 0, &Xkb_sync, 
#ifndef MINIMAL
		"Xkb state interaction: 0=none, 1=sync, 2-semi-sync"
#endif
	},
	{ 'l', IAM ".no_lock", 2, 0, &no_lock, "disable modifiers lock" },
	{ 'o', IAM ".output", 0, 0, &output,
#ifdef USE_XR
		"xrandr output name"
#endif
	},
#ifdef CACHE_PIX
	{ 'C', IAM ".cache", 1, 0, &cache_pix, "pixmap cache 0=disable, 1=enable, 2=preload, 3=direct+cache\n	(3 is optimal, but sometimes start on black bar)" },
#endif
#ifdef USE_XI
	{ 'z', IAM ".z", 2, 0, &use_pressure, "use z (pressure) if possible, experimental:\n	-1=no verify, 0=no, 1=yes, <device>" },
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
   printf("Usage: " IAM " <options>\n\
Options:\n\
  -d  <display>\n\
  -x  used for gtk embedding\n\
  -c  dock\n\
  -s  strut\n\
  -v  version\n\
  -e {<...>}   exec on restart (to end of line)\n\
  -h  this help\n");
				for (res=0; (res1=&resources[res])->param; res++) {
					char *t,*h=res1->help?:"";
					void *p;
					if (!res1->name) continue;
					switch (res1->type) {
					case 0:printf("  -%c <%s>  (string%s%s) %s\n",res1->param, res1->name, *(char **)res1->ptr?"=":"", *(char **)res1->ptr?:"", h);break;
					case 1:printf("  -%c <%s>  (int=%i) %s\n",res1->param, res1->name, *(int *)res1->ptr, h);break;
					case 2:printf("  -%c [<%s: 1>]  ([int]=%i) %s\n",res1->param, res1->name, *(int *)res1->ptr, h);break;
					}
				}
				exit(0);
			}
		};
		if (!res1->ptr) continue;
		res1->ch++;
		switch (res1->type) {
		    case 0:
			*(char **)res1->ptr = *argv[++i]?argv[i]:NULL;
			break;
		    case 1:
		    case 2:
			if (!argv[++i]) goto no_more_param;
			j = strtol(argv[i],&r,10);
			if (!*r && r!=argv[i]) {
				*(int *)res1->ptr = j;
			} else if (res1->type == 1){
				fprintf(stderr,"%s: invalid option value: %s %s\n",iam,s,argv[i]);
//				exit(1);
			} else {
				i--;
no_more_param:
				*(int *)res1->ptr = 1;
			}
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

   char *rs;

   if ((rs = XResourceManagerString(display)) && (xrm = XrmGetStringDatabase(rs))) {
    for (i=0; s=(res1=&resources[i])->name; i++) {
	char *type = NULL;
	XrmValue val;
	int n;
	val.addr = NULL;

	if (res1->ptr && !res1->ch && XrmGetResource(xrm,s,NULL,&type,&val) && val.addr) {
		switch (res1->type) {
		case 0:
			*(char **)(res1->ptr) = NULL;
			n = strlen((char *)val.addr)+1;
			if (n>1) memcpy(*(char **)(res1->ptr) = malloc(n), val.addr, n);
			break;
		case 1:
		case 2:
			j = strtol((char *)val.addr,&r,10);
			if (!*r && r!=(char *)val.addr) {
				*(int *)res1->ptr = j;
			} else if (res1->type == 1){
				fprintf(stderr,"%s: invalid xrdb value: %s %s\n",iam,s,(char *)val.addr);
//				exit(1);
			};
			break;
		}
	}
    }
    XrmDestroyDatabase(xrm);
  }
  XFree(rs);

      Atom wm_protocols[]={
	_atom("WM_DELETE_WINDOW"),
	_atom("WM_PROTOCOLS"),
	_atom("WM_NORMAL_HINTS"),
      };
      atype = _atom("_NET_WM_WINDOW_TYPE");
      Atom astate = _atom("_NET_WM_STATE");
      aStrut = _atom("_NET_WM_STRUT");
      aStrutPartial = _atom("_NET_WM_STRUT_PARTIAL");
      aDock = _atom("_NET_WM_WINDOW_TYPE_DOCK");
      if(dock & 2048) aActions = _atom("_NET_WM_ALLOWED_ACTIONS");

#ifdef USE_XR
	int xrevent = -100, xrevent1, xrerror, xrr;
	xrr = XRRQueryExtension(display, &xrevent, &xrerror);
#ifdef RRCrtcChangeNotifyMask
	xrevent1 = xrevent + RRNotify;
#endif
	xrevent += RRScreenChangeNotify;
#endif
#ifdef USE_XI
	int xiopcode, xievent = 0, xierror, xi, ximajor = 2, ximinor = 4;
	xi = !(fake_touch&1)
		&& XQueryExtension(display, "XInputExtension", &xiopcode, &xievent, &xierror)
		&& XIQueryVersion(display, &ximajor, &ximinor) != BadRequest;
#endif
#ifndef MINIMAL
	// not found how to get keymap change on XInput, so keep Xkb events
	int xkbError, reason_rtrn, xkbmjr = XkbMajorVersion, xkbmnr = XkbMinorVersion, xkbop;
	int DeviceIdMask = (fake_touch&2) ? XIAllMasterDevices : XIAllDevices;
	if (!XkbQueryExtension(display,&xkbop,&xkbEventType,&xkbError,&xkbmjr,&xkbmnr)) xkbEventType = 0;
#endif

chScreen:
   screen = DefaultScreen(display);
   rootWin = RootWindow(display, screen);
   scr_width = DisplayWidth(display,screen);
   scr_height = DisplayHeight(display,screen);
   scr_mwidth = DisplayWidthMM(display,screen);
   scr_mheight = DisplayHeightMM(display,screen);

   XGetWindowAttributes(display,rootWin,&wa0);

   X1=wa0.x; Y1=wa0.y; X2=wa0.x+wa0.width-1; Y2=wa0.y+wa0.height-1;
   if (scr_mwidth > scr_mheight && scr_width < scr_height && scr_mheight) {
	unsigned long sw = scr_mwidth;
	scr_mwidth = scr_mheight;
	scr_mheight = sw;
   }

#ifdef USE_XR
	// find actual output;
	// if no geometry, try to avoid strut in the middle of overlapped outputs (-> top/left).
	if (xrr) {
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
				int x2=cinf->width;
				int y2=cinf->height;
				for (j=0; j<xrrr->nmode; j++) {
					XRRModeInfo *m = &xrrr->modes[j];
					if (m->id==cinf->mode) {
						x2=m->width;
						y2=m->height;
						break;
					}
				}
				x2+=x1-1;
				y2+=y1-1;
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
				subpixel_order = oinf->subpixel_order;
				scr_mwidth=oinf->mm_width;
				scr_mheight=oinf->mm_height;
				if (scr_mwidth > scr_mheight && x2-x1 < y2-y1 && scr_mheight) {
					scr_mwidth=oinf->mm_height;
					scr_mheight=oinf->mm_width;
				}
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

      /* check for user selected keyboard conf file */

      if (!remapped) {
        if (conf_file == NULL) {
	  strcpy(userconffile,getenv("HOME"));
	  strcat(userconffile, "/.");
	  strcat(userconffile, iam);

	  if ((fp = fopen(userconffile, "r")) != NULL) {
		conf_file = (char *)malloc(sizeof(char)*512);
		if (fgets(conf_file, 512, fp) != NULL) {
			fclose(fp);
			if ( conf_file[strlen(conf_file)-1] == '\n')
				conf_file[strlen(conf_file)-1] = '\0';
		}
	  } else conf_file = DEFAULTCONFIG;
	}
	win = XCreateSimpleWindow(display, rootWin, x, y, w, h,
		0, 0, BlackPixel(display, screen));
	XSetStandardProperties(display, win, window_name,
		icon_name, None, argv, argc, None);

      _reset();
      _signals(signal_exit);
      }
#ifdef USE_XR
      else {
        kb = NULL;
	for (geo = geo0; geo; geo = geo->next) {
		if (geo->w == scr_width && geo->h == scr_height && geo->mw == scr_mwidth && geo->mh == scr_mheight && geo->subpixel_order == subpixel_order) {
			kb = geo->kb;
			break;
		}
	}
      }
#endif

      if (!kb) {
	kb = kb_new(win, display, screen, 0, 0, width, height, conf_file, font_name, font_name1);
	kb_size(kb);
      }
      i=kb->vbox->act_width;
      j=kb->vbox->act_height;
      if (width != i || height != j) {
        int k;
        k = x + w - i;
	if (!left && k>=X1 && k<=X2) x = k;
        k = y + h - j;
	if (!top && k>=Y1 && k<=Y2) y = k;
	width=i;
	height=j;
      }
	_move(x,y,width,height); // +hints
//      if (cache_pix) kb_repaint(kb); // reduce blinking on start

      if (remapped) goto _remapped;

	hide_enabled = 1;
      //hide_enabled = dock & (4|2048);
      if(dock & 4)
        _prop(32,atype,XA_ATOM,&aDock,1,PropModeAppend);
      if (dock & 8)
	_propAtom32(atype,"_NET_WM_WINDOW_TYPE_TOOLBAR");
      if (dock & 16)
	_propAtom32(astate,"_NET_WM_STATE_STICKY");
      if (dock & 128)
	_propAtom32(astate,"_NET_WM_STATE_SKIP_TASKBAR");
      if (dock & 256)
	_propAtom32(astate,"_NET_WM_STATE_ABOVE");
//      _propAtom32(astate,"_NET_WM_STATE_FOCUSED");
//      Atom version = 4;
//      __prop(32,"XdndAware",XA_ATOM,&version,1);
      if (dock&512) {
	CARD32 dsks = 0xffffffff;
	_propCard32(_atom("_NET_WM_DESKTOP"),&dsks,1);
      }
      if (dock&1024)
	_propAtom32(astate,"_NET_WM_STATE_SKIP_PAGER");

      Atom mwm_atom = _atom("_MOTIF_WM_HINTS");
      XChangeProperty(display,win, mwm_atom, mwm_atom,32,PropModeReplace,(unsigned char *)&prop,5);
      prop[0] = 0;

      static XWMHints wm_hints = { .input = False, .flags = InputHint };
      if (dock & 1) {
	wm_hints.flags |= IconWindowHint | WindowGroupHint | StateHint;
	wm_hints.initial_state = WithdrawnState;
	wm_hints.icon_window = wm_hints.window_group = win;
      }
      XSetWMHints(display, win, &wm_hints);

      XSetWMProtocols(display, win, wm_protocols, sizeof(wm_protocols) /
		      sizeof(Atom));

#ifndef MINIMAL
	// not found how to get keymap change on XInput, so keep Xkb events
	if (xkbEventType) {
		unsigned int m = XkbNewKeyboardNotifyMask;
		if (Xkb_sync) m|=XkbStateNotifyMask;
		XkbSelectEvents(display,XkbUseCoreKbd,XkbAllEventsMask,m);
		if (m & XkbStateNotifyMask)
			XkbSelectEventDetails(display,XkbUseCoreKbd,XkbStateNotifyMask,XkbAllStateComponentsMask,XkbModifierStateMask|XkbModifierLatchMask|XkbModifierLockMask|XkbModifierBaseMask);
	}
#endif

#ifdef USE_XR
      if (xrr) XRRSelectInput(display, win, RRScreenChangeNotifyMask
#ifdef RRCrtcChangeNotifyMask
	|RRCrtcChangeNotifyMask
	|RROutputChangeNotifyMask
//	|RROutputPropertyNotifyMask
#endif
	);
#endif

#ifdef USE_XI
      // keep it constant to compile-out unused event filtering
      if(xi) {
        // right: ((XI_LASTEVENT+7)/8), but 4 + XI_GestureSwipeEnd -> BadValue
#define XI_MASK_LEN XIMaskLen(XI_LASTEVENT)
	static unsigned char mask_[XI_MASK_LEN] = {};
	static XIEventMask mask = { .mask_len = XI_MASK_LEN, .mask = (unsigned char *)&mask_ };
	mask.deviceid = DeviceIdMask;

	if (!(fake_touch&4)) XISetMask(mask.mask, XI_Motion);
	XISetMask(mask.mask, XI_ButtonPress);
	XISetMask(mask.mask, XI_ButtonRelease);

	XISetMask(mask.mask, XI_TouchBegin);
	XISetMask(mask.mask, XI_TouchUpdate);
	XISetMask(mask.mask, XI_TouchEnd);
#if defined(XI_GestureSwipeBegin) && defined(GESTURES_USE)
	if (ximinor > 3 || ximinor > 2) {
		XISetMask(mask.mask, XI_GestureSwipeBegin);
		XISetMask(mask.mask, XI_GestureSwipeUpdate);
		XISetMask(mask.mask, XI_GestureSwipeEnd);
	}
#endif
	XISelectEvents(display, win, &mask, 1);
      } else
#endif
	// may be lost "release" on libinput
	// probably need all motions or nothing
	// button events required for mouse only or if XI not used
//	Button1MotionMask |
      evmask|=ButtonPressMask|ButtonReleaseMask|((fake_touch&4)?0:ButtonMotionMask);


      setWA.override_redirect = !!(dock & 4096);
//      XSelectInput(display, win, evmask);
      XChangeWindowAttributes(display, win, CWOverrideRedirect|CWEventMask, &setWA);

      signal(SIGUSR1, handle_sig); /* for extenal mapping / unmapping */
      signal(SIG_HIDE, _hide);
      signal(SIG_SHOW, _hide);

      XSetErrorHandler(xerrh);

_remapped:
      if (embed) {
	 fprintf(stdout, "%li\n", win);
	 fclose(stdout);
      } else {
	XMapWindow(display, win);
      }
      XFlush(display);

      while (1)
      {
	    int type = 0;
	    XNextEvent(display, &ev);
//	    if (ev.type<36) fprintf(stderr,"+%i ",ev.type);
	    switch (ev.type) {
#ifdef USE_XI
		case GenericEvent: if (ev.xcookie.extension == xiopcode
//		    && ev.xgeneric.extension == 131
		    && XGetEventData(display, &ev.xcookie)
		    ) {
#undef e
#define e ((XIDeviceEvent*)ev.xcookie.data)
			if (e->sourceid != e->deviceid && DeviceIdMask == XIAllDevices) break;
			// protect from fusion button/touch events
			static int lastid = -1;
			int ex = e->event_x;
			int ey = e->event_y;
			z_t ez = (use_pressure
				&& abs3(e->sourceid)
				&& e->valuators.mask_len > z_byte && (e->valuators.mask[z_byte]&z_mask == z_mask)
				) ? (e->valuators.values[z_number] - z_min) : 0;
			switch(ev.xcookie.evtype) {
			    case XI_ButtonRelease: type++;
			    case XI_Motion: type++; // always detail==0
			    case XI_ButtonPress:
				if (lastid == e->sourceid) {
					if (!e->detail) break;
					lastid = -1;
				}
#ifndef GESTURES_USE
				{
#else
				fprintf(stderr,"+ %i %i,%i %i %i\n",e->sourceid,ex,ey,e->detail,type);
				switch (e->detail) {
				    case 7: //XkbLockModifiers(display,XkbUseCoreKbd,STATE(KBIT_CAPS),kb->state ^ STATE(KBIT_CAPS));break;
				    case 6: //XkbLockGroup(display,XkbUseCoreKbd,kb->group+1); break;
				    case 5:
				    case 4:
					_hide(sig[e->detail]);
					break;
				    default: if (!resized)
#endif
				fprintf(stderr,"++++++++++++ %i,%i %i %i\n",ex,ey,e->detail,type);
					active_but = kb_handle_events(kb, type, ex, ey, ez, e->detail, e->sourceid, e->time,e->buttons.mask,e->buttons.mask_len);
				}
				break;
			    case XI_TouchUpdate:
				if (!(e->flags & XITouchPendingEnd)) {
					active_but = kb_handle_events(kb, 1, ex, ey, ez, e->detail, lastid = e->sourceid, e->time, NULL, 0);
					break;
				}
			    case XI_TouchEnd:
				active_but = kb_handle_events(kb, 2, ex, ey, ez, e->detail, lastid = e->sourceid, e->time, NULL, 0);
				break;
			    case XI_TouchBegin:
#ifdef GESTURES_USE
				if (resized) {
					_hide(SIG_SHOW);
					break;
				}
#endif
				active_but = kb_handle_events(kb, 0, ex, ey, ez, e->detail, lastid = e->sourceid, e->time, NULL, 0);
				break;
#if defined(XI_GestureSwipeBegin) && defined(GESTURES_USE)
			    case XI_GestureSwipeBegin:
			    case XI_GestureSwipeUpdate:
			    case XI_GestureSwipeEnd:
				fprintf(stderr,"swipe\n");
				break;
#endif
			}
			XFreeEventData(display, &ev.xcookie);
		}
		break;
#endif
	    case ButtonRelease: type=2;
	    case ButtonPress:
#ifndef GESTURES_USE
		{
#else
		switch (ev.xbutton.button) {
		    case 7: //XkbLockModifiers(display,XkbUseCoreKbd,STATE(KBIT_CAPS),kb->state ^ STATE(KBIT_CAPS));break;
		    case 6: //XkbLockGroup(display,XkbUseCoreKbd,kb->group+1); break;
		    case 5:
		    case 4:
			_hide(sig[ev.xbutton.button]);
			break;
		    default: if (!resized)
#endif
			active_but = kb_handle_events(kb, type, ev.xbutton.x, 0, ev.xbutton.y, ev.xbutton.button, 0, ev.xbutton.time, &ev.xbutton.state, sizeof(ev.xbutton.state));
		}
		break;
	    case MotionNotify:
		active_but = kb_handle_events(kb, 1, ev.xmotion.x, ev.xmotion.y, 0, 0, 0, ev.xmotion.time, &ev.xmotion.state, sizeof(ev.xmotion.state));
		break;
	    case ClientMessage:
		if ((ev.xclient.message_type == wm_protocols[1])
		      && (ev.xclient.data.l[0] == wm_protocols[0]))
		{
//			xkbd_destroy(kb);
			XCloseDisplay(display);
			exit(0);
		}
		break;
	    case ConfigureNotify:
		kb->act_width = ev.xconfigure.width;
		kb->act_height = ev.xconfigure.height;
		if (resized) break;
		if (ev.xconfigure.width != kb->vbox->act_width
		    || ev.xconfigure.height != kb->vbox->act_height)
		{
			kb_resize(kb,
				 ev.xconfigure.width,
				 ev.xconfigure.height );
		}
		break;
	    case Expose:
		if (rootChanged(&wa)) goto chScreen;
		if (resized) break;
		if (unmapWin()) {
			XMapWindow(display, win);
			continue;
		} 
		transCoord();
		setSize(x = kb->X, y = kb->Y, width, height, resized);
		reset2();
		break;
//	    case MapNotify:
//	    case UnmapNotify:
//		if (rootChanged(&wa)) goto chScreen;
//		break;
	    case VisibilityNotify: if (dock & 32) {
		Window rw, pw, *wins, *ww;
		unsigned int nw,re = 0;
		static int lock_cnt = 0;

		if (ev.xvisibility.state!=VisibilityFullyObscured ||
			display!=ev.xvisibility.display ||
			win!=ev.xvisibility.window ||
			!XQueryTree(display, rootWin, &rw, &pw, &wins, &nw)
			) break;
		ww = wins;
		while (nw--) {
			if (XGetWindowAttributes(display, *ww,&wa) &&
				wa.screen == wa0.screen &&
				wa.x<=x && wa.y<y && wa.width>=width && wa.height>height &&
				wa.x+wa.width>x && wa.y+wa.height>y
				&& *ww != rootWin && XResizeWindow(display, *ww, wa.width, y-wa.y))
					re++;
			ww++;
		}
		XFree(wins);
		// first lock: fork (to realize init-lock safe wait)
		if (re && !lock_cnt++ && fork()) exit(0);
	    }
	    if ((dock & 256) && ev.xvisibility.state!=VisibilityUnobscured) {
			if (rootChanged(&wa)) goto chScreen;
			XRaiseWindow(display, win);
	    }
	    if (dock & (256|32)) XFlush(display);
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
				if (kb_sync_state(kb,e.mods,e.locked_mods,e.group))
					kb_repaint(kb);
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
#undef e
#define e ((XRRScreenChangeNotifyEvent*)&ev)
		static unsigned long xrserial = 0;
		if (ev.type == xrevent) {
			// reduce blinking, but possible bugs (?)
//			if (e->serial == xrserial) continue; xrserial = e->serial;

			unmapOrRestart();
			subpixel_order = e->subpixel_order;
			XRRUpdateConfiguration(&ev);
			goto chScreen;
		}
#ifdef RRCrtcChangeNotifyMask
#undef e
#define e ((XRRNotifyEvent*)&ev)
		if (ev.type == xrevent1) {
			// reduce blinking, but possible bugs (?)
//			if (e->serial == xrserial) continue; xrserial = e->serial;

			unmapOrRestart();
			goto chScreen;
		}
#endif
#endif
	    }
	    while (kb_do_repeat(kb, active_but) && !XPending(display))
		usleep(10000L); /* sleep for a 10th of a second */
      }
no_dpy:
	fprintf(stderr, "%s: cannot connect to X server\n", argv[0]);
	exit(1);
}
