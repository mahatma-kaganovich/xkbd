/*
	xtg v1.52 - per-window keyboard layout switcher [+ XSS suspend].
	Common principles looked up from kbdd http://github.com/qnikst/kbdd
	- but rewrite from scratch.

	I found some problems in kbdd (for example - startup WM detection),
	CPU usage and strange random bugs...
	But looking in code - I found this is terrible, overcoded and required
	to be much simpler.

	I use window properties for state tracking, unify WM model
	and print layout name to STDIN per line to use with tint2.

	Default layout always 0: respect single point = layout config order.


	As soon it use root events & current window tracking, code
	reused for other features. Default changed to compile all of them.

	Use -DNO_ALL to keep simple keyboard layout tracking.

	XSS suspend (-DXSS):
	Disable XScreenSaver if fullscreen window focused.
	Added to xkbdd as massive code utilization.

	Auto hide/show mouse cursor on touchscreen.
	Gestures.
	Auto RandR DPI.
	Auto primary or vertical panning (optional).
	-h to help

	(C) 2019-2021 Denis Kaganovich, Anarchy license
*/
#ifndef NO_ALL
#define XSS
#define XTG
//#define TOUCH_ORDER
//#define USE_EVDEV
//#undef USE_EVDEV
#define SYSFS_CACHE
#define _BACKLIGHT 2
#define PROP_FMT
//#define _UNSAFE_WINMAP
#undef PROP_FMT
//#define TRACK_MATRIX
#define FAST_VALUATORS
// as soon Z!/pressure or legacy unhooked floating starts to be actual - set it.
// -f 7|15 will set per device, not XIAllDevices
#endif



#if !_BACKLIGHT || defined(MINIMAL)
#undef SYSFS_CACHE
#undef USE_XTHREAD
#undef USE_PTHREAD
#endif



#include <stdio.h>
#include <stdint.h>

#include <X11/Xproto.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>

#ifdef XSS
#define USE_DPMS
#define USE_XSS
#ifdef USE_DPMS
#include <X11/extensions/dpms.h>
#endif
#ifdef USE_XSS
#include <X11/extensions/scrnsaver.h>
#endif
#endif

#ifdef XTG
#include <signal.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/randr.h>
//#include <X11/extensions/XInput.h>
#ifdef USE_EVDEV
#include <libevdev/libevdev.h>
#endif
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XI2proto.h>

#include <X11/extensions/XTest.h>
#include <X11/extensions/Xfixes.h>
#include <stdlib.h>
#include <string.h>
//#include <sys/time.h>
#include <X11/Xpoll.h>
// internal extensions list. optional
#include <X11/Xlibint.h>
#if _BACKLIGHT
#include <glob.h>
#include <limits.h>

#undef USE_THREAD
#ifdef USE_XTHREAD
#include <X11/Xthreads.h>
#include <sys/inotify.h>
#define USE_THREAD
#elif defined(USE_PTHREAD)
#include <pthread.h>
#include <sys/inotify.h>
#define xthread_fork(f,c) { pthread_t _th; pthread_create(&_th,NULL,f,c); }
#define USE_THREAD
#endif

#endif
#endif


#define NO_GRP 99

#define BUTTON_DND 2
#define BUTTON_HOLD 3
#define MAX_FINGERS 8

#define BUTTON_LEFT 7
#define BUTTON_RIGHT 6
#define BUTTON_UP 5
#define BUTTON_DOWN 4

#define BUTTON_KEY 15

#define PH_BORDER (1<<2)

#define ERR(txt, args... ) fprintf(stderr, "Error: " txt "\n", ##args )
#ifndef NDEBUG
#define DBG(txt, args... ) fprintf(stderr, "" txt "\n", ##args )
#else
#define DBG(txt, args... ) /* nothing */
#endif

#if 0
#define RECT_EQ(a,b) ((a).x == (b).x && (a).y == (b).y && (a).width == (b).width && (a).height == (b).height && (a).border_width == (b).border_width)
#define RECT_MV(a,b) {(a).x = (b).x; (a).y = (b).y; (a).width = (b).width; (a).height = (b).height; (a).border_width = (b).border_width;}
#define RECT(a) a.x-(a).border_width,a.y-(a).border_width,a.width+((a).border_width<<1),a.height+((a).border_width<<1)
#else
#define RECT_EQ(a,b) ((a).x == (b).x && (a).y == (b).y && (a).width == (b).width && (a).height == (b).height)
#define RECT_MV(a,b) {(a).x = (b).x; (a).y = (b).y; (a).width = (b).width; (a).height = (b).height;}
#define RECT(a) a.x,a.y,a.width,a.height
#endif

typedef uint_fast8_t _short;
typedef uint_fast32_t _uint;

#if 0
typedef int _int;
#define fint "%i"
#else
typedef long _int;
#define fint "%li"
#endif


Display *dpy = 0;
Window win = None, win1 = None;
int screen;
Window root;
Atom aActWin, aKbdGrp, aXkbRules;
unsigned char _error;

#undef _WIN_STATE

// for lazy coding
#define _a(var,name) (var?:(var=XInternAtom(dpy, name, False)))
#define _A(var,name) static Atom var
#define _A1(var,name) _A(var,name); var = _a(var,name);

#ifdef XSS
int xssevent=-100;
Atom aWMState, aFullScreen;
Bool noXSS, noXSS1;
#ifdef USE_XSS
_short xss_enabled;
#else
#define xss_enabled 0
#endif

#ifdef USE_DPMS
_short dpms_enabled;
int dpmsopcode=0, dpmsevent=-100;
#else
#define dpms_enabled 0
#endif
#endif

CARD32 grp, grp1;
unsigned char *rul, *rul2;
unsigned long rulLen, n;
int xkbEventType=-100, xkbError, n1;
XEvent ev;
unsigned char *ret;

// min/max: X11/Xlibint.h
static inline _int _min(_int x,_int y){ return x<y?x:y; }
static inline _int _max(_int x,_int y){ return x>y?x:y; }

#ifdef XTG
int devid = 0;
int xiopcode=0, xfopcode=0, xfevent=-100;
Atom aFloat,aMatrix,aABS,aDevMon;
XRRScreenResources *xrrr = NULL;
_short showPtr, oldShowPtr = 0, curShow;
int floatFmt = sizeof(float) << 3;
int atomFmt = sizeof(Atom) << 3;
int winFmt = sizeof(Window) << 3;
_short useRawTouch = 0, useRawButton = 0;
int bits_per_rgb = 0;

// >=0 remove extra pixels, add extra mode compare
// <0 to add extra pixels to screen to precise rounding DPI, relaxed compare
#define _DPI_DIFF -1
#define _DPI_CH(w0,h0,w,h,alt) ((alt) && (_DPI_DIFF<0 || (abs((w)-(w0))*254+.5>_DPI_DIFF || abs((h)-(h0))*254+.5>_DPI_DIFF)))

_int w_width = 0, w_height = 0, w_mwidth = 0, w_mheight = 0;
double w_dpmh = 0, w_dpmw = 0;
#define _w_none (_short)(1)
//#define _w_screen (_short)(1<<1)
#define _w_screen 0
#define _w_init (_short)(1<<2)
#define _w_pan (_short)(1<<3)
_short _wait_mask = _w_none|_w_init;

#define INCH 25.4
#define DPI_EQ(w,h) {if (!(pi[p_safe]&512)) { if (h > w) w = h; else h = w; }}

#define prop_int int32_t
int intFmt = sizeof(prop_int) << 3;
#ifdef USE_EVDEV
Atom aNode;
#endif
#ifdef XSS
XWindowAttributes wa;
#endif

// right: ((XI_LASTEVENT+7)/8), but 4 + XI_GestureSwipeEnd -> BadValue
#define MASK_LEN XIMaskLen(XI_LASTEVENT)
typedef unsigned char xiMask[MASK_LEN];
xiMask ximaskButton = {}, ximaskTouch = {}, ximask0 = {}, ximaskRaw = {};
xiMask ximaskRoot = {};
_short Nximask = 1;
XIEventMask ximask[] = {{ .deviceid = XIAllDevices, .mask_len = MASK_LEN, .mask = ximask0 },
//			 { .deviceid = XIAllMasterDevices, .mask_len = MASK_LEN, .mask = ximaskRoot }
			};
//#define MASK(m) ((void*)ximask[0].mask=m)
#if _BACKLIGHT
_short backlight = 0;
#endif
#if _BACKLIGHT == 1
xiMask ximaskWin0 = {};
XIEventMask ximaskWin[] = {{ .deviceid = XIAllMasterDevices, .mask_len = MASK_LEN, .mask = ximaskWin0 },
//			 { .deviceid = XIAllDevices, .mask_len = MASK_LEN, .mask = ximaskWin0 }
			};
#if 0
#define _WIN_STATE
Atom aAbove, aBelow;
#endif
#endif

Time T;
#define TIME(T,t) (T = (t))

#define TOUCH_SHIFT 5
#define TOUCH_MAX (1<<TOUCH_SHIFT)
#define TOUCH_MASK (TOUCH_MAX-1)
#define TOUCH_N(x) (((x)+1)&TOUCH_MASK)
#define TOUCH_P(x) (((x)+TOUCH_MASK)&TOUCH_MASK)
#define TOUCH_CNT ((N+TOUCH_MAX-P)&TOUCH_MASK)
typedef struct _Touch {
	int touchid,deviceid;
	Time time;
	_short n, g, g1, z;
	double x,y;
	double tail;
} Touch;
Touch touch[TOUCH_MAX];
_short P=0,N=0;

XIAttachSlaveInfo ca = {.type=XIAttachSlave};
XIDetachSlaveInfo cf = {.type=XIDetachSlave};
XIAddMasterInfo cm = {.type = XIAddMaster, .name = "TouchScreen", .send_core = 0, .enable = 1};
XIRemoveMasterInfo cr = {.type = XIRemoveMaster, .return_mode = XIFloating};
int kbdDev = 0;

_short end;

double resX, resY;
int resDev = 0;
int xrr, xropcode=0, xrevent=-100, xrevent1=-100;
Atom aMonName = None;

Time timeSkip = 0, timeHold = 0;

#define MAX_BUTTON 255
#define END_BIT ((MAX_BUTTON+1)>>1)
#define BAD_BUTTON END_BIT
typedef struct _TouchTree {
	void *gg[8];
	_uint k;
	_short g;
} TouchTree;
TouchTree bmap = {};
static void SET_BMAP(_uint i, _short x, _uint key){
	static TouchTree *bmap_ = &bmap;;
	TouchTree **m = &bmap_;
	for(; i; i>>=3) {
		m = (void*) &((*m)->gg[i&7]);
		if (!*m) *m = calloc(1, sizeof(**m));
	}
	(*m)->g = x;
	(*m)->k = key;
}

static void opendpy();
static void _print_bmap(_uint x, _short n, TouchTree *m) {
	_short i;
	unsigned int l = m->g;
	if (l) printf(" 0%o:%s%x",x,l>9?"0x":"",l);
	l = m->k;
	if (l) {
		if (!dpy) opendpy();
		printf(":%s[0x%x]",dpy ? XKeysymToString(XkbKeycodeToKeysym(dpy,l,0,0)) : "",l);
	}
	for(i=0; i<8; i++) {
		if (m->gg[i]) _print_bmap(x|(i<<n),n+3,m->gg[i]);
	}
}



#ifdef USE_EVDEV
#define DEF_RR -1
#else
#define DEF_RR 0
#endif

#define DEF_SAFE (1+32+64)
#define DEF_SAFE2 (2+4)

#define p_device 0
#define p_minfingers 1
#define p_maxfingers 2
#define p_hold 3
#define p_xy 4
#define p_res 5
#define p_round 6
#define p_floating 7
#define p_mon 8
#define p_touch_add 9

#define p_min_native_mon 10
#define p_max_native_mon 11
#define p_min_dpi 12
#define p_max_dpi 13
#define p_dpi 14
#define p_safe 15
#define p_Safe 16
#define p_content_type 17
#define p_colorspace 18
#define p_1 19
#define p_y 20
#define p_Y 21
#define p_help 22
#define MAX_PAR 23
char *pa[MAX_PAR] = {};
// android: 125ms tap, 500ms long
#define PAR_DEFS { 0, 1, 2, 1000, 2, -2, 0, 1, DEF_RR, 0,  14, 32, 72, 0, 0, DEF_SAFE, DEF_SAFE2, 0, 0, 0 }
int pi[MAX_PAR] = PAR_DEFS;
double pf[] = PAR_DEFS;
char pc[] = "d:m:M:t:x:r:e:f:R:a:o:O:p:P:i:s:S:c:C:1yYh";
char *ph[MAX_PAR] = {
	"touch device 0=auto", //d

	"min fingers", //m

	"max fingers", //M

	"button 2|3 hold time, ms", //t

	"min swipe x/y or y/x", //x

	"swipe size (>0 - dots, <0 - -mm)", //r

	"add to coordinates (to round to integer)", //e

	"	1=floating devices\n"
	"		0=master (visual artefacts, but enable native masters touch clients)\n"
	"		2=dynamic master/floating (firefox segfaults)\n"
	"		3=simple hook all touch events (useful +4=7 - raw)\n"
	"		+4=use XI_RawTouch* events if possible\n"
	"		+8=use XI_RawButton* & XI_RawTouch* events if Z (pressure)\n"
	"			=15 - always Raw events (emulation/debug)\n"
	"		(really use XI_Raw* both 4 and 8, but result device related)\n"
	"		(I have Wacom 5020 pen to 3D experiments)"
	, //f

	"RandR monitor name\n"
	"		or number 1+\n"
	"		'-' strict (only xinput prop 'xtg output' name)"
#ifdef USE_EVDEV
	"\n		or negative -<x> to find monitor, using evdev touch input size, grow +x\n"
#endif
	"			for (all|-d) absolute pointers (=xinput map-to-output)", //R
	"map-to-output add field around screen, mm (if -R)"
#ifdef USE_EVDEV
	"\n		or \"+\" to use input-output size diff (may be just float-int truncation)",
#endif
	"min native dpi monitor (<0 - -horizontal mm, >0 -  diagonal \")",
	"max ...",
	"min dpi (monitor)",
	"max dpi",
	"default dpi - basic dpi, some apps use hardcoded 96, not real dots/inch (0)",
	"safe mode bits\n"
	"		0(+1) don't change hierarchy of device busy/pressed (for -f 2)\n"
#ifdef USE_EVDEV
	"		1(+2) XUngrabServer() for libevdev call\n"
#endif
	"		2(+4) XGrabServer() cumulative\n"
	"		3(+8) cursor recheck (tricky)\n"
	"		4(+16) no mon dpi\n"
	"		5(+32) no mon primary\n"
	"		6(+64) no vertical panning && auto-on && ondemand XI2 prop 'scaling mode' = Full\n"
	"		7(+128) auto bits 5,6 on primary present - enable primary & disable panning\n"
	"		8(+256) use mode sizes for panning (errors in xrandr tool!)\n"
	"		9(+512) keep dpi different x & y (image panned non-aspected)\n"
	"		10(+1024) don't use cached input ABS\n"
	"		11(+2048) try delete or not create unused (saved) property\n"
#if _BACKLIGHT
	"		12(+4096) track backlight-controlled monitors and off when empty (under construction)\n"
	"			modesetting: chmod o+w /sys/class/drm/card0-*/*backlight*/brightness\n"
	"			- to emulate XR 'Backlight' prop\n"
#endif	
#ifdef XSS
	"		13(+8192) skip fullscreen windows without ClientMessage (XTerm)\n"
#endif
	"		14(+16384) XRRSetCrtcConfig() move before panning\n",
	"Safe mode bits 2\n"
	"		0(+1) minimize screen size changes in pixels (dont reduce, dont round to DPI)\n"
	"		1(+2) don't ajust values, based on deps ('max bpc')\n"
#ifdef XSS
	"		2(+4) don't set fullscreen 'Broadcast RGB: Limited 16:235'\n"
#endif
#ifdef SYSFS_CACHE
	"		3(+8) keep sysfs 'brightness' file(s) opened (may lockup sysfs)'\n"
#endif
	"\n	(safe bits tested on xf86-video-intel)\n",
#ifdef XSS
	"fullscreen \"content type\" prop.: see \"xrandr --props\" (I want \"Cinema\")",
	"fullscreen \"Colorspace\" prop.: see \"xrandr --props\"  (I want \"DCI-P3_RGB_Theater\")\n",
#endif
	" oneshot (implies set DPI, panning, etc) - to run & exit before WM",
	" preset max-conservative",
	" preset max-experimental",
	" help",
};
#else
#define TIME(T,t)
#endif


static void opendpy() {
	int reason_rtrn, xkbmjr = XkbMajorVersion, xkbmnr = XkbMinorVersion;
	dpy = XkbOpenDisplay(NULL,&xkbEventType,&xkbError,&xkbmjr,&xkbmnr,&reason_rtrn);
#ifdef XTG
	if (!dpy) return;
	int xievent = 0, ierr;
	xiopcode = 0;
	XQueryExtension(dpy, "XInputExtension", &xiopcode, &xievent, &ierr);
#endif
}

Atom pr_t;
int pr_f;
unsigned long pr_b,pr_n;


#define _free(p) if (p) { XFree(p); p=NULL; }

char *_aret[] = {NULL,NULL,NULL,NULL,NULL};
static char *natom(_short i,Atom a){
	if(_aret[i]) XFree(_aret[i]);
	return _aret[i] = XGetAtomName(dpy,a);
}


static Bool getRules(){
	_free(rul);
	rulLen = 0;
	n1 = 0;
	if (XGetWindowProperty(dpy,root,aXkbRules,0,256,False,XA_STRING,&pr_t,&pr_f,&rulLen,&pr_b,&rul)==Success && rul && rulLen>1 && pr_t==XA_STRING) {
		rul2 = rul;
		return True;
	}
	rul2 = NULL;
	return False;
}

static int getWProp(Window w, Atom prop, Atom type, int size){
	_free(ret);
	if (!(XGetWindowProperty(dpy,w,prop,0,1024,False,type,&pr_t,&pr_f,&n,&pr_b,&ret)==Success && pr_f>>3 == size && ret))
		n=0;
	return n;
}

static void printGrp(){
	int i, n2 = 0;
	unsigned char *s, *p, c;

	if (rul2 || getRules()) {
		p = s = rul2;
		for (i=rulLen-(rul2-rul); i; i--) {
			switch (*(s++)) {
			    case '\0':
				if ((n2==grp1 && n1==2) || n1>2) break;
				n1++;
				n2=0;
				p=s;
				if (n1==2) rul2 = s;
				continue;
			    case ',':
				if (n2==grp1 && n1==2) break;
				n2++;
				p=s;
				continue;
			    default:
				continue;;
			}
			break;
		}
	}
	if (n1==2 && n2==grp1) {
		s--;
		c = *s;
		*s = '\0';
		fprintf(stdout,"%s\n",p);
		*s = c;
	} else {
		rul2 = NULL;
		fprintf(stdout,"%lu\n",(unsigned long)grp1);
	}
	fflush(stdout);
}

static void _set_same_size(){
	XWindowChanges c;
	Window r = None;
	int depth;

	if (!XGetGeometry(dpy,root,&r,&c.x,&c.y,&c.width,&c.height,&c.border_width,&depth) || r != root) r = None;
#ifdef XTG
	if (!xrr || (pi[p_safe]&(16|64)) != (16|64)) {
		oldShowPtr |= 8;
		goto xlib;
	}
	if (!r) {
		c.width = w_width;
		c.height = w_height;
		r = root;
	} else if (c.width != w_width || c.height != w_height) {
		oldShowPtr |= 8;
		goto xlib;
	}
	// "empty" (same size) event
	XRRSetScreenSize(dpy,root,w_width,w_height,w_mwidth,w_mheight);
xlib:
#endif
	// no same size event
//	if (r) XReconfigureWMWindow(dpy,root,screen,CWWidth|CWHeight,&c);
}

#ifdef XSS
// DPMSInfo() and XScreenSaverQueryInfo() result is boolean (but type is "Status")
// use transparent values instead
#ifdef USE_XSS
#ifdef XTG
// known2me 2 ways to detect vterm switch:
// 1) XSS state off -> off
// 2) inotify /sys/devices/virtual/tty/tty0/active
// now (1) used
static void chTerm(){
	oldShowPtr |= 2;
	if ((pi[p_safe]&(16|64)) == (16|64)) oldShowPtr |= 8;
	// screen changed event
	else _set_same_size();
}
#endif
_short xssState = 3;
#if ScreenSaverDisabled == 3 && ScreenSaverOn == 1 && ScreenSaverOff == 0
static void xssStateSet(_short s){
#ifdef XTG
	if (!(xssState|(xssState = s))) chTerm();
#else
	xssState = s;
#endif
}
#else
static void xssStateSet(_short s){
	switch (s) {
	    case ScreenSaverOff: 
#ifdef XTG
		if (!xssState) chTerm();
#endif
		xssState = 0;
		break;
	    case ScreenSaverOn: xssState = 1; break;
	    case ScreenSaverCycle: xssState = 2; break;
//	    case ScreenSaverDisabled:
	    default:
		    xssState = 3; break; // or dpms
	}
}
#endif
static _short xss_state(){
	XScreenSaverInfo *x = XScreenSaverAllocInfo();
	if (x) {
		x->state = ScreenSaverDisabled;
		XScreenSaverQueryInfo(dpy, root, x);
		xssStateSet(x->state);
		XFree(x);
	}
	return xssState;
}
#endif
#ifdef USE_DPMS
// to xss style
static _short dpms_state(){
	BOOL en = 0;
	CARD16 s = 0;
	DPMSInfo(dpy,&s,&en);
	return en ? (s != DPMSModeOn) : 3;
}
#endif

#ifdef XTG

typedef union _xrp_ {
	prop_int i;
	Atom a;
} _xrp_t;

#define XRP_EQ(x,y) ((sizeof(prop_int) == sizeof(Atom) || type_xrp[prI] == XA_ATOM) ? (x.a == y.a) : (x.i == y.i))

typedef enum {
	xrp_non_desktop,
	xrp_sm,
	xrp_bpc,
#if _BACKLIGHT
	xrp_bl,
#endif
#ifdef XSS
	xrp_ct,
	xrp_cs,
	xrp_rgb,
#endif
	xrp_cnt,
} xrp_t;

char *an_xrp[xrp_cnt] = {
	RR_PROPERTY_NON_DESKTOP,
	"scaling mode",
	"max bpc",
#if _BACKLIGHT
	RR_PROPERTY_BACKLIGHT,
#endif
#ifdef XSS
	"content type",
	"Colorspace",
	"Broadcast RGB",
#endif
};

Atom type_xrp[xrp_cnt] = {
	XA_INTEGER,
	XA_ATOM,
	XA_INTEGER,
#if _BACKLIGHT
	XA_INTEGER,
#endif
#ifdef XSS
	XA_ATOM,
	XA_ATOM,
	XA_ATOM,
#endif
};

_xrp_t val_xrp[xrp_cnt] = {};

Atom a_xrp[xrp_cnt], a_xrp_save[xrp_cnt];

_short xrProp_ch = 0;
static void xrrSet(){
	if (!xrrr && xrr) {
		xrrr = XRRGetScreenResources(dpy,root);
		xrProp_ch = 0;
	}
}

static void xrrFree(){
	if (xrrr && xrr) {
		XRRFreeScreenResources(xrrr);
		xrrr = NULL;
	}
}

static _short getGeometry(){
//	return XGetWindowAttributes(dpy, win, &wa);
	return XGetGeometry(dpy,win,&wa.root,&wa.x,&wa.y,&wa.width,&wa.height,&wa.border_width,&wa.depth);
}


static void monFullScreen();
static void xrPropFlush();

#endif
static void WMState(Atom state, _short op){
	int i = (state == aFullScreen);
	if (!i && !noXSS) return;
	switch (op) {
	    case 0: // delete
	    case 1: // add
		if (i) noXSS1 = op;
		break;
	    case 2: // toggle
		noXSS1 = i;
		break;
	    default:
		noXSS1 = 0;
		getWProp(win,aWMState,XA_ATOM,sizeof(Atom));
		for (i=0; i<n; i++) if (((Atom*)ret)[i]==aFullScreen) {
			noXSS1 = 1;
			break;
		}
	}
	if (noXSS1!=noXSS) {
		noXSS=noXSS1;
#ifdef USE_XSS
		if (xss_enabled
//			&& xssState != 3
			) XScreenSaverSuspend(dpy,noXSS);
		else
#endif
#ifdef USE_DPMS
		if (dpms_enabled) {
			static _short on = 0;
			if (noXSS) {
				if ((on = (dpms_state()!=3))) DPMSDisable(dpy);
			} else if (on) DPMSEnable(dpy);
		}
#endif
		;
#ifdef XTG
		monFullScreen();
#endif
	}
}

static void winWMState(){
	WMState(aFullScreen,7);
}
#endif

#ifdef XTG

_short grabcnt = 0;
//_uint grabserial = -1;
static void _Xgrab(){
//		grabserial=(grabserial+1)&0xffff;
		XGrabServer(dpy);
}
static void _Xungrab(){
		XUngrabServer(dpy);
//		grabserial=(grabserial+1)&0xffff;
}
static void _grabX(){
	if (!(grabcnt++)) {
		XFlush(dpy);
		XSync(dpy,False);
		_Xgrab();
	}
}
static inline void forceUngrab(){
	if (grabcnt) {
		XFlush(dpy);
		_Xungrab();
		XSync(dpy,False);
		grabcnt=0;
	}
}
static inline void _ungrabX(){
	if (!(pi[p_safe]&4) && !--grabcnt) {
		XFlush(dpy);
		_Xungrab();
	}
}

#define NdevABS 3

typedef struct _abs {
	// "max" used only as "max - min"
	_short en;
	double min,max_min;
	int resolution;
	int number;
	_short np,nm;
} abs_t;

abs_t xABS[NdevABS], *xabs1, *xabs2;

typedef struct _dinf {
	_short type;
	int devid;
	float ABS[NdevABS];
	abs_t xABS[NdevABS];
	Atom mon;
	int attach0;
	_short reason;
#ifdef FAST_VALUATORS
	_short fast;
	double fastABS[NdevABS];
#endif
#ifdef TRACK_MATRIX
	float matrix[9];
#endif
	xiMask evmask;
	double z,minZ,maxZ,mindiffZ;
	_short zstate,z_ewmh;
	int zdetail;
	unsigned long zserial;
	Time ztime;
} dinf_t;

#define z_nothing 0
#define z_inc 1
#define z_dec 2
#define z_start 3


static inline double _valuate(double val, abs_t *a, _int width) {
	return (a->max_min > 0) ? (val - a->min)*width/a->max_min : val;
}

#define o_master 1
#define o_floating 2
#define o_absolute 4
//#define o_changed 8
#define o_scroll 16
#define o_touch 32
#define o_directTouch 64
#define o_kbd 128

#define DEV_CMP_MASK (o_absolute|o_scroll|o_touch|o_directTouch)
#define TDIRECT(type) ((type&(o_directTouch|o_scroll))==o_directTouch)


#define NINPUT_STEP 4
dinf_t *inputs = NULL, *inputs1, *dinf, *dinf1, *dinf2, *dinf_last = NULL;
_uint ninput = 0, ninput1 = 0;
#define DINF(x) for(dinf=inputs; (dinf!=dinf_last); dinf++) if (x)

typedef struct _pinf {
	struct {
		_short en : 1;
		_short my : 1;
	};
	XRRPropertyInfo *p;
	_xrp_t v,v1,vs[2];
} pinf_t;

pinf_t *pr;
xrp_t prI;

// width/height: real/mode
// width0/height0: -||- or first preferred (default/max) - safe bit 9(+256)
// width1/height1: -||- not rotated (for panning)
typedef struct _minf {
	_short type;
	_int x,y,x2,y2,width,height,width0,height0,width1,height1;
	_int mwidth,mheight,mwidth1,mheight1;
	RROutput out;
	RRCrtc crt;
	RRMode mode;
	Rotation rotation;
	_short r90;
	Atom name;
	_short connection;
	pinf_t prop[xrp_cnt];
#if _BACKLIGHT == 1
	_short obscured,entered;
	Window win;
#endif
#if _BACKLIGHT == 2
	unsigned long obscured;
	_short entered;
#endif
#ifdef SYSFS_CACHE
	int blfd; // sysfs file + 1
#endif
} minf_t;

//#define o_out 1
#define o_non_desktop 2
#define o_active 4
#define o_changed 8
#define o_size 16
#define o_msize 32
#define o_backlight 64
#define o_primary 128

double dpm0w,dpm0h;
minf_t minf0, *minf = NULL, *minf_last = NULL, *minf1 = NULL, *minf2 = NULL;
#define MINF1(x) for(minf=outputs; (minf!=minf_last) x; minf++)
#define MINF2(x,y) for(minf=outputs; (minf!=minf_last) x; minf++) if (y)
//#define MINF(x) if (noutput) for(minf=outputs; (minf!=minf_last) && fprintf(stderr,"+%s\n",natom(0,minf->name)); minf++) if (x)
#define MINF(x) for(minf=outputs; (minf!=minf_last); minf++) if (x)
// 1 - out, 2 - non_desktop, 4 - connected
// (2|4)=6 - exists
#define MINF_T(x) MINF((minf->type & (x)))
#define MINF_T2(and,eq) MINF((minf->type & (and)) == (eq))
#define MINF_PR(x) for(minf=outputs; (minf!=minf_last); minf++) if ((pr=&minf->prop[x])->en)

// usual dpi calculated from height only"
_short resXY,mon_sz,strict;
Atom mon = 0;
double mon_grow;


#if 0
#define TIMEVAL(tv,x) struct timeval tv = { .tv_sec = (x) / 1000, .tv_usec = ((x) % 1000) * 1000 }
#else
#define TIMEVAL(tv,x) struct timeval tv = { .tv_sec = (x) >> 10, .tv_usec = ((x) & 0x3ff) << 10 }
#endif

static int _delay(Time delay){
	if (QLength(dpy)) return 0;
	if (grabcnt) {
		XFlush(dpy);
		_Xungrab();
		grabcnt=0;
		XSync(dpy,False);
		if (XPending(dpy)) return 0;
	}
	TIMEVAL(tv,delay);
	fd_set rs;
	int fd = ConnectionNumber(dpy);
	FD_ZERO(&rs);
	FD_SET(fd,&rs);
	return !Select(fd+1, &rs, 0, 0, &tv);
}

static int fmtOf(Atom type,int def){
	return (type==XA_STRING
//		||type==XA_CARDINAL
		)?8:(type==aFloat)?floatFmt:(type==XA_ATOM)?atomFmt:(type==XA_INTEGER)?intFmt:(type==XA_WINDOW)?winFmt:def;
}

#ifdef PROP_FMT
static void *fmt2fmt(void *from,Atom fromT,int fromF,void *to,Atom toT,int toF,unsigned long cnt){
	if (fromT == toT && fromF == toF && !to) return from;
	if ((fromT != XA_INTEGER && fromT != XA_CARDINAL) || (toT != XA_INTEGER && toT != XA_CARDINAL)) return NULL;
	switch (fromF) {
	case 64:
	case 32:
	case 16:
	case 8: break;
	default: return NULL;
	}
	switch (toF) {
	case 64:
	case 32:
	case 16:
	case 8: break;
	default: return NULL;
	}

	unsigned long i;
	long long x;
	void *ret1 = ret;

	if (!to) to = ret = malloc((toF>>3)*cnt);
	for (i=0; i<cnt; i++) {
		if (fromT == XA_INTEGER) {
			switch (fromF) {
			case 64: x = ((int64_t*)from)[i]; break;
			case 32: x = ((int32_t*)from)[i]; break;
			case 16: x = ((int16_t*)from)[i]; break;
			case 8: x = ((int8_t*)from)[i]; break;
			}
		} else {
			switch (fromF) {
			case 64: x = ((uint64_t*)from)[i]; break;
			case 32: x = ((uint32_t*)from)[i]; break;
			case 16: x = ((uint16_t*)from)[i]; break;
			case 8: x = ((uint8_t*)from)[i]; break;
			}
		}
		if (toT == XA_INTEGER) {
			switch (toF) {
			case 64: ((int64_t*)to)[i] = x; break;
			case 32: ((int32_t*)to)[i] = x; break;
			case 16: ((int16_t*)to)[i] = x; break;
			case 8: ((int8_t*)to)[i] = x; break;
			}
		} else {
			switch (toF) {
			case 64: ((uint64_t*)to)[i] = x; break;
			case 32: ((uint32_t*)to)[i] = x; break;
			case 16: ((uint16_t*)to)[i] = x; break;
			case 8: ((uint8_t*)to)[i] = x; break;
			}
		}
	}
	if (ret != ret1 && ret1) XFree(ret1);
	return to;
}
#endif

// chk return 2=eq +:
// 1 - read+cmp, 1=ok
// 2 - - (to check event or xiSetProp() force)
// 3 - 1=incompatible (xiSetProp() strict)
// 4 - 1=incompatible or none (xiSetProp() strict if exists)
// *SetProp: |0x10 - check (read) after write

char *_pr_name;
static _short _chk_prop(char *pr, unsigned long who, Atom prop, Atom type, unsigned char **data, long cnt, _short chk){
	if (!ret) return (chk>3);
	if (pr_b || (cnt>0 && pr_n!=cnt)) goto err;
	if (!type && !*data && !chk) return 1;
	int f = fmtOf(pr_t,-1);
	if (pr_t != type || pr_f != f) {
#ifdef PROP_FMT
		if (!chk) {
			if (!fmt2fmt(ret,pr_t,pr_f,*data,type,f,pr_n)) goto err;
			return 1;
		} else if (!fmt2fmt(ret,pr_t,pr_f,NULL,type,f,pr_n)) goto err;
		else
#endif
		{
err:
			ERR("incompatible %s() result of %lu %s %s %i",pr,who,natom(0,prop),natom(1,pr_t),pr_f);
			return (chk>2);
		}
	}
	if (*data) {
		if (type == XA_STRING) pr_n++;
		pr_n*=(f>>3);
		if (chk && !memcmp(*data,ret,pr_n)) return 2;
		else if (chk<2) memcpy(*data,ret,pr_n);
		else return 0;
	} else *data = ret;
	return 1;
}

typedef enum {
	pr_win,
	pr_out,
	pr_input,
} pr_target_t;

pr_target_t target;

static _short getProp(Atom prop, Atom type, void *data, int pos, long cnt, _short chk){
	_free(ret);
	int N = abs(cnt), r;
	long detail;
	_error = 0;
	switch (target) {
	    case pr_win:
		_pr_name = "XGetWindowProperty";
		detail = win;
		r = XGetWindowProperty(dpy,win,prop,pos,N,False,type,&pr_t,&pr_f,&pr_n,&pr_b,(void*)&ret);
		break;
	    case pr_out:
		_pr_name = "XRRGetOutputProperty";
		detail = minf->out;
		Bool pending = False;
		r = XRRGetOutputProperty(dpy,minf->out,prop,pos,N,False,pending,type,&pr_t,&pr_f,&pr_n,&pr_b,(void*)&ret);
		break;
	    case pr_input:
		_pr_name = "XIGetOutputProperty";
		detail = devid;
		r = XIGetProperty(dpy,devid,prop,0,N,False,type,&pr_t,&pr_f,&pr_n,&pr_b,(void*)&ret);
		break;
	    default:
		ERR("BUG!");
		return 0;
	}
	if (r != Success) {
		ERR("%i %s %s %li",r,_pr_name,natom(0,prop),detail);
	}
	if ((chk&0x10) && !_error) {
//		XFlush(dpy);
		XSync(dpy,False);
	}
	return _chk_prop(_pr_name,minf->out,prop,type,(void*)&data,cnt,chk&0x0f);
}


static _short setProp(Atom prop, Atom type, int mode, void *data, long cnt, _short chk){
	int f = fmtOf(type,32);
	if (chk&0xf) {
		_short r;
		if ((r=getProp(prop,type,data,0,cnt,chk))) return (r==2)<<1;
#ifdef PROP_FMT
		data = fmt2fmt(data,type,f,NULL,pr_t,pr_f,cnt);
		type = pr_t;
		f = pr_f;
#endif
	}
	if (!data) return 0;
	if ((chk&0x10)) {
		XFlush(dpy);
		XSync(dpy,False);
	}
	_error = 0;
	switch (target) {
	    case pr_win:
		XChangeProperty(dpy,win,prop,type,f,mode,data,cnt);
		break;
	    case pr_out:
		XRRChangeOutputProperty(dpy,minf->out,prop,type,f,mode,data,cnt);
		break;
	    case pr_input:
		XIChangeProperty(dpy,devid,prop,type,f,mode,data,cnt);
		break;
	    default:
		ERR("BUG!");
		return 0;
	}
	XFlush(dpy);
	if ((chk&0x10) && !_error) {
//		XFlush(dpy);
		XSync(dpy,False);
	}
	return !_error;
}


static _short xrGetProp(Atom prop, Atom type, void *data, long cnt, _short chk){
	target = pr_out;
	return getProp(prop,type,data,0,cnt,chk);
}

static _short xrSetProp(Atom prop, Atom type, void *data, long cnt, _short chk){
	target = pr_out;
	return setProp(prop,type,PropModeReplace,data,cnt,chk);
}

static _short xiGetProp(Atom prop, Atom type, void *data, long cnt, _short chk){
	target = pr_input;
	return getProp(prop,type,data,0,cnt,chk);
}

static void _xrPropFlush(){
	xrrFree();
	XFlush(dpy);
	XSync(dpy,False);
	xrrSet();
	xrrFree();
}

static void xrPropFlush(){
	if (xrProp_ch) _xrPropFlush();
}

static _short xiSetProp(Atom prop, Atom type, void *data, long cnt, _short chk){
	target = pr_input;
	return setProp(prop,type,PropModeReplace,data,cnt,chk);
}

#define __max_wins 0x7fffffff
static _short wGetProp(Atom prop, Atom type, void *data, long cnt, _short chk){
	target = pr_win;
	return getProp(prop,type,data,0,cnt,chk);
}

static _short wGetProp_(Atom prop, Atom type, void *data, long cnt, Window w){
	target = pr_win;
	win = w;
	_short r = getProp(prop,type,data,0,cnt,0);
	win = win1;
	return r;
}

static _short wSetProp(Atom prop, Atom type, void *data, long cnt, _short chk){
	target = pr_win;
	return setProp(prop,type,PropModeReplace,data,cnt,chk);
}

static void xiSetMask(xiMask mask,int event) {
	static xiMask msk = {};
	if (!mask) mask = msk;
	if (event) XISetMask(mask,event);
	ximask[0].mask = (void*)mask;
	ximask[0].deviceid = devid;
	XISelectEvents(dpy, root, ximask, 1);
	if (event) XIClearMask(mask,event);
	ximask[0].mask = NULL;
}

#ifdef USE_EVDEV
static _short _evsize(struct libevdev *dev, int c, int i) {
	const struct input_absinfo *x = libevdev_get_abs_info(dev, c);
	if (!x) return 0;
	int mm = x->maximum - x->minimum;
	if (i == 2) {
		if (dinf->ABS[i] = mm) return 1;
		if (!dinf->xABS[i].en && dinf->xABS[1].en) oldShowPtr |= 2;
		dinf->ABS[i] = mm;
		if (x->resolution) dinf->ABS[i]/=x->resolution;
		return 1;
	}
	if (!x->resolution) return 0;
	dinf->ABS[i] = (mm + .0)/x->resolution;
	return 1;
}

static void getEvRes(){
	devid = dinf->devid;
	if (!(pi[p_safe]&1024) && xiGetProp(aABS,aFloat,&dinf->ABS,NdevABS,0)) return;
	memset(&dinf->ABS,0,sizeof(dinf->ABS));
	if (!xiGetProp(aNode,XA_STRING,NULL,-1024,0)) return;
	if (grabcnt && (pi[p_safe]&2)) _Xungrab();
	int fd = open(ret, O_RDONLY|O_NONBLOCK);
	if (fd < 0) goto ret;
	struct libevdev *device;
	if (!libevdev_new_from_fd(fd, &device)){
		if (_evsize(device,ABS_MT_POSITION_X,0) && _evsize(device,ABS_MT_POSITION_Y,1)) {
			if (!_evsize(device,ABS_MT_PRESSURE,2)) goto _z;
		} else if (_evsize(device,ABS_X,0) && _evsize(device,ABS_Y,1)) {
_z:
			if (_evsize(device,ABS_PRESSURE,2) || _evsize(device,ABS_Z,2));
		}
		libevdev_free(device);
		xiSetProp(aABS,aFloat,&dinf->ABS,NdevABS,0);
	}
	close(fd);
ret:
	if (grabcnt && (pi[p_safe]&2)) _Xgrab();
}
#endif

static void map_to(){
	devid = dinf->devid;
	double x=minf->x,y=minf->y,w=minf->width1,h=minf->height1,dx=pf[p_touch_add],dy=pf[p_touch_add];
	_short m = 1;

	if (pa[p_touch_add] && pa[p_touch_add][0] == '+' && pa[p_touch_add][1] == 0) {
		if (minf->mwidth1 && dinf->ABS[0]!=0.) dx = (dinf->ABS[0] - minf->mwidth1)/2;
		if (minf->mheight1 && dinf->ABS[1]!=0.) dy = (dinf->ABS[1] - minf->mheight1)/2;
	}
	if (dx!=0. && minf->mwidth1) {
		dx *= w/minf->mwidth1;
		x-=dx;
		w+=dx*2;
	}
	if (dy!=0. && minf->mheight1) {
		dy *= h/minf->mheight1;
		y-=dy;
		h+=dy*2;
	}

	if (minf->r90) {
		x/=minf0.width1;
		y/=minf0.height1;
		h/=minf0.width1;
		w/=minf0.height1;
	} else {
		x/=minf0.width1;
		y/=minf0.height1;
		w/=minf0.width1;
		h/=minf0.height1;
	}

	switch (minf->rotation) {
	    case RR_Reflect_X|RR_Rotate_0:
	    case RR_Reflect_Y|RR_Rotate_180:
		x+=w; w=-w;
		break;
	    case RR_Reflect_Y|RR_Rotate_0:
	    case RR_Reflect_X|RR_Rotate_180:
		y+=h; h=-h;
		break;
	    case RR_Rotate_90:
	    case RR_Rotate_270|RR_Reflect_X|RR_Reflect_Y:
		x+=w; w=-w; m=0;
		break;
	    case RR_Rotate_270:
	    case RR_Rotate_90|RR_Reflect_X|RR_Reflect_Y:
		y+=h; h=-h; m=0;
		break;
	    case RR_Rotate_90|RR_Reflect_X:
	    case RR_Rotate_270|RR_Reflect_Y:
		m=0;
		break;
	    case RR_Rotate_90|RR_Reflect_Y:
	    case RR_Rotate_270|RR_Reflect_X:
		m=0;
	    case RR_Rotate_180:
	    case RR_Reflect_X|RR_Reflect_Y|RR_Rotate_0:
		x+=w; y+=h; w=-w; h=-h;
		break;
	}
	float matrix[9] = {0,0,x,0,0,y,0,0,1};
	matrix[1-m]=w;
	matrix[3+m]=h;
#ifdef TRACK_MATRIX
	if (!memcmp(dinf->matrix,matrix,sizeof(matrix))) return;
	if (xiSetProp(aMatrix,aFloat,&matrix,9,2)!=1) return;
	memcpy(dinf->matrix,matrix,sizeof(matrix));
	// wacom touch + pen sometimes changed paired, confused me
	DBG("input %i map_to "fint"x"fint" "fint"x"fint" rotation 0x%x from "fint"x"fint" -> m=%i w=%f h=%f",devid,minf->x,minf->y,minf->height1,minf->height1,minf->rotation,minf0.width1,minf0.height1,m,w,h);
	//XFlush(dpy);
	//XSync(dpy,False);
#else
	xiSetProp(aMatrix,aFloat,&matrix,9,4);
#endif
}

static void fixMonSize(minf_t *minf, double *dpmw, double *dpmh) {
	if (!minf->mheight) return;
	double _min, _max, m,  mh = minf->height1 / (minf->r90?*dpmw:*dpmh);

	// diagonal -> 4:3 height
	if (
	    (pi[p_min_native_mon] && mh < (m = (m = pf[p_min_native_mon]) < 0 ? -m : (m * (INCH * 3 / 5)))) ||
	    (pi[p_max_native_mon] && mh > (m = (m = pf[p_max_native_mon]) < 0 ? -m : (m * (INCH * 3 / 5))))
		) {
		m /= mh;
		*dpmw /= m;
		*dpmh /= m;
	}

	if (pi[p_max_dpi]) {
		_max = pf[p_max_dpi] / INCH;
		if (*dpmw > _max) {
			m = _max / *dpmw;
			*dpmw = _max;
			*dpmh /= m;
		}
		if (*dpmh > _max) {
			m = _max / *dpmh;
			*dpmw /= m;
			*dpmh = _max;
		}
	}
	if (pi[p_max_dpi]) {
		_min = pf[p_min_dpi] / INCH;
		if (*dpmw < _min) {
			m = _min / *dpmw;
			*dpmw = _min;
			*dpmh /= m;
		}
		if (*dpmh < _min) {
			m = _min / *dpmh;
			*dpmw /= m;
			*dpmh = _min;
		}
	}
}

minf_t *outputs = NULL;
int noutput = 0;

#define _BUFSIZE 1048576



static void pr_set(xrp_t prop,_short state);

static void _sysfs_free(){
#ifdef SYSFS_CACHE
	if (minf->blfd > 0) close(minf->blfd - 1);
	minf->blfd = 0;
#endif
}

#if _BACKLIGHT

#ifdef USE_THREAD
int inotify;
static void *thread_inotify(void* args){
	struct inotify_event ie;
	while(read(inotify, &ie, sizeof(ie))) {
		oldShowPtr |= 8;
	}
}
#endif

#ifdef O_DIRECT
#define _o_sync (O_DIRECT)
#elif defined(SYSFS_CACHE)
#define _o_sync (O_DSYNC)
#else
#define _o_sync (0)
#endif

char *_mon_sysfs_name;
int _mon_sysfs_name_len;
unsigned _sysfs_val;
static int _sysfs_open(_short mode) {
	char bn[10];
	int fd = -3;

	if (_mon_sysfs_name_len > 64) goto ex;
#ifdef MINIMAL
	char buf[128];
	static char *nn[] = {
		"/intel_backlight/brightness",
		"/intel_backlight/max_brightness",
	};
	strcpy(buf,"/sys/class/drm/card0-");
	memcpy(&buf[21],_mon_sysfs_name,_mon_sysfs_name_len);
	strcpy(&buf[21+_mon_sysfs_name_len],nn[mode]);
	fd = open(buf,mode?(O_RDONLY|O_NONBLOCK|_o_sync):(O_RDWR|O_NONBLOCK|_o_sync));
#else
	static char *buf = NULL, *buf0;
	buf = buf?:malloc(128+NAME_MAX);
	if (mode) {
		strcpy(buf0,"max_brightness");
		fd = open(buf,O_RDONLY|O_NONBLOCK|_o_sync);
	} else {
		buf0 = strcpy(buf,"/sys/class/drm/card*-");
		buf0 += 21;
		memcpy(buf0,_mon_sysfs_name,_mon_sysfs_name_len);
		buf0 += _mon_sysfs_name_len;
		strcpy(buf0,"/*backlight*/brightness");
		glob_t pg = {};
		size_t n = 0;
		if (!glob(buf,GLOB_NOSORT,NULL,&pg)) {
			int l;
			if (pg.gl_pathc == 1 && (l = strlen(pg.gl_pathv[0]) - 10) < 128+256-10+4) {
#ifdef USE_THREAD
				//inotify_add_watch(inotify,pg.gl_pathv[0],IN_MODIFY|IN_ATTRIB|IN_MOVE|IN_CREATE|IN_DELETE|IN_UNMOUNT);
				inotify_add_watch(inotify,pg.gl_pathv[0],IN_ATTRIB|IN_MOVE|IN_CREATE|IN_DELETE|IN_UNMOUNT);
#endif
				if ((fd = open(pg.gl_pathv[0],O_RDWR|O_NONBLOCK|_o_sync)) >= 0) {
					memcpy(buf,pg.gl_pathv[0],l);
					buf0 = buf + l;
				}
			} else if (pg.gl_pathc > 1) for (l = 0; l < pg.gl_pathc; l++) {
				// more checks possible (only writable & max_brightness)
				ERR("extra backlights matched: %s",pg.gl_pathv[l]);
			}
		}
		if (pg.gl_pathv)
			globfree(&pg);
	}
#endif

	if (fd < 0) goto ex;
	ssize_t n = read(fd,&bn,sizeof(bn));
	if (n < 1 || n == sizeof(bn) || bn[--n] != '\n') goto err;
	_sysfs_val = 0;
	for(_short i = 0; i < n; i++) {
		if (bn[i] < '0' || bn[i] > '9') goto err;
		_sysfs_val = _sysfs_val*10 + (bn[i] - '0');
	}
	goto ex;
err:
	close(fd);
	fd = -3;
ex:
	return fd;
}

static void _bl_sysfs(){
	if (!(pi[p_safe]&4096)) goto ex;
	_xrp_t cur;
	static long values[] = {0, 0};
	int fd, fd1;
#ifdef SYSFS_CACHE
	if (minf->blfd) {
		if (minf->blfd == -1) goto ex;
		fd = minf->blfd - 1;
		cur.i = pr->v1.i;
		_sysfs_val = pr->p->values[1];
		if (!lseek(fd,0,0)) goto files_ok;
		_sysfs_free();
	}
#endif
	if (!_mon_sysfs_name) {
		_mon_sysfs_name = natom(0,minf->name);
		_mon_sysfs_name_len = strlen(_mon_sysfs_name);
	}
	fd = _sysfs_open(0);
	if (fd < 0) {
#ifdef SYSFS_CACHE
		// if not found - recheck only on rescan monitors
		// xf86-video-intel still multiple rescan on EACCES
		if (fd == -1 && errno == ENOENT) minf->blfd = -1;
#endif
		goto ex;
	}
	cur.i = _sysfs_val;
	fd1 = _sysfs_open(1);
	if (fd1 < 0) goto err;
	close(fd1);
#ifdef SYSFS_CACHE
	if ((pi[p_Safe]&8))
	    minf->blfd = fd + 1;
#endif
files_ok:
	if (pr->p && (!pr->p->range || pr->p->values[0] != 0 || pr->p->values[1] != _sysfs_val)) goto err;
	if (pr->en) {
		long new = pr->v.i;
		if (cur.i != new) {
			if (new < 0 || new > _sysfs_val) new = _sysfs_val;
			if (dprintf(fd,"%lu\n",new) <= 0) goto err;
		}
	} else {
		// init/pre-configure
		values[1] = _sysfs_val;
		if (!pr->p) {
			XRRConfigureOutputProperty(dpy,minf->out,a_xrp[xrp_bl],False,True,2,(long*)&values);
			pr->my = 1;
		}
		xrSetProp(a_xrp[xrp_bl],XA_INTEGER,&cur,1,!!pr->p);
		DBG("backlight over sysfs: %s cur=%i max=%u",_mon_sysfs_name,cur.i,_sysfs_val);
	}
#ifdef SYSFS_CACHE
	if (minf->blfd) goto ex;
err:
	minf->blfd = 0;
#else
err:
#endif
	close(fd);
ex:
	_mon_sysfs_name = NULL;
}

#if _BACKLIGHT == 2

Window *wins = NULL, *winf, *wins_last = NULL;
unsigned int nwins;
XWindowAttributes wa1;
#define WINS(x) for(winf=wins; winf!=wins_last; winf++) if (*winf != root && (x))

// less difference to keep array or create/delete every time. keep!
static void freeWins(){
	_free(wins);
	wins_last = NULL;
	nwins = 0;
}
static void getWins(){
	if (wins) return;
	Window rw, pw;
	//DBG("rescan XQueryTree() event %i",ev.type);
	if (!XQueryTree(dpy, root, &rw, &pw, &wins, &nwins) || !wins) {
		_free(wins);
	} else {
		wins_last = &wins[nwins];
	}
}

#define WINS_ATTR() freeWins(); getWins(); while (wins_attr())

static _short chWin(Window w){
	if (ev.xany.window == root && backlight) {
		getWins();
		WINS(*winf == w) return 1;
	}
	return 0;
}

static _short wa1Mapped(Window w){
	wa1.map_state = IsUnmapped;
	wa1.root = None;
	return XGetWindowAttributes(dpy,w,&wa1) && wa1.map_state == IsViewable && wa1.root == root;
};

XConfigureEvent evconf = {};
Window winMapped = None;
_short stMapped = 0;

static void chBL0(){
	pr_set(xrp_bl,!(minf->obscured || minf->entered));
}

static void chBL(Window win,_int x,_int y,_int w,_int h, _short mode) {
	if (!backlight) return;
	minf_t *minf1; // recursive
#define minf minf1
	MINF_T2(o_active|o_backlight,o_active|o_backlight) {
#undef minf
		_short r = !(minf1->y >= y+h || minf1->y2 < y || minf1->x2 < x || minf1->x >= x+w);
		switch (mode&0xf) {
		    case 0: // add pointer
			minf1->entered = r;
			break;
		    case 1: // del (?) pointer
			minf1->entered = 0;
			break;
		    case 2: // add rectangle(s)
			if (r) minf1->obscured++;
			break;
		    case 3: // del/rescan window(s)
			if (win) {
				if (r) minf1->obscured--;
				//evconf.window = None;
				break;
			}

			// reset/rescan all
			win = None;
			MINF(1) minf->entered = minf->obscured = 0;
			getWins();
			//DBG("rescan XGetWindowAttributes() event %i",ev.type);
#ifdef MINIMAL
			WINS(wa1Mapped(*winf)) chBL(*winf,RECT(wa1),0x12);
#else
			// optimized
			_short ec = 0;
			WINS(1) {
#if 0
				if (*winf == evconf.window && *winf == winMapped) {
					chBL(evconf.window,RECT(evconf),0x12);
					ec = 1;
					continue;
				}
#endif
				_short st1 = wa1Mapped(*winf);
				if (st1) chBL(*winf,RECT(wa1),0x12);
				// paranoid check
				if (evconf.window == *winf && RECT_EQ(evconf,wa1)) ec=1;
				if (*winf == winMapped && st1 != stMapped) winMapped = None;
			}
			if (!ec) evconf.window = None;
#endif
			if (wins) {
				mode = 5;
				break;
			}
			mode = 4;
		    case 4: // fallback
			minf1->entered = 0;
			minf1->obscured = 1;
			break;
		}
		minf = minf1;
		if (!(mode&0x10)) chBL0();
	}
}

static void setMapped(Window w,_short st) {
	if (!backlight) return;
	if (ev.xany.window != root) DBG("ev.xany.window != root: ev %i win %lx",ev.type,ev.xany.window);
	if (ev.type == DestroyNotify) {
		oldShowPtr |= 64;
		if (stMapped) oldShowPtr |= 32;
		if (winMapped == w) winMapped = None;
		return;
	}
	switch (ev.type) {
	    case ConfigureNotify:
#undef e
#define e (ev.xconfigure)
		if (evconf.window == e.window && RECT_EQ(evconf,e)) return;
		if (winMapped != w) {
			st = wa1Mapped(w);
			if (!wa1.root) goto err;
			stMapped = st;
			winMapped = w;
		};
		if (!stMapped);
		else if (evconf.window != e.window || (oldShowPtr&64)) oldShowPtr |= 32;
		else {
			chBL(evconf.window,RECT(evconf),0x13);
			chBL(e.window,RECT(e),2);
		}
		evconf = e;
		return;
	    case CreateNotify:
#undef e
#define e (ev.xcreatewindow)
		oldShowPtr |= 64; // ??
		memcpy(&evconf,&e,_min(sizeof(evconf),sizeof(e)));
		//evconf.window = e.window;
		//RECT_MV(evconf,e);

		if (winMapped != w) {
//#ifdef _UNSAFE_WINMAP
			st = wa1Mapped(w);
			if (!wa1.root) goto err;
			oldShowPtr |= 64;
			if (st) goto ch;
			goto OK;
//#endif
		} else {
			winMapped == None;
			oldShowPtr |= 64|32;
		}
		return;
//	    case DestroyNotify:
//		DBG("Destroy");
//		break;
	}
	if (winMapped == w && stMapped == st) {
		DBG("remapped? ev=%i %i was=%i",ev.xany.type,st,stMapped);
		winMapped = None;
		oldShowPtr |= 32;
		return;
	}
#ifndef _UNSAFE_WINMAP
	if (w == evconf.window) goto ch;
#endif
	_short st1 = wa1Mapped(w);
	if (!wa1.root) {
err:
		if (chWin(w)) oldShowPtr |= 32|64;
		if (winMapped == w) winMapped = None;
		return;
	}
	if (st != st1) {
		DBG("mapped? ev=%i %i attr=%i",ev.xany.type,st,st1);
		winMapped = None;
		oldShowPtr |= 32|64;
		return;
	}
#ifdef _UNSAFE_WINMAP
	if (w == evconf.window && !st) {
		chBL(w,RECT(evconf),3);
		RECT_MV(evconf,wa1);
		goto OK;
	}
#endif
	evconf.window = w;
	RECT_MV(evconf,wa1);
ch:
	chBL(w,RECT(evconf),st ? 2 : 3);
OK:
	winMapped = w;
	stMapped = st;
}

#endif

#if _BACKLIGHT == 1
static void chBL1(_short obscured, _short entered) {
	if (obscured != 2) minf->obscured = obscured;
	if (entered != 2) minf->entered = entered;
	pr_set(xrp_bl,!(minf->obscured || minf->entered));
}

static void chBL(Window w, _short obscured, _short entered) {
	if (w == root) return;
//	MINF(minf->win == w && (minf->type&o_backlight)) {
	MINF(minf->win == w) {
		chBL1(obscured,entered);
		break;
	}
}
#endif

#if 0
#define __check_entered
static void checkEntered(){
	_short i;
	for (i=0; i<2; i++) DINF((dinf->type&(o_master|o_kbd))==o_master) {
		Window rw,rc;
		XIButtonState b;
		XIModifierState m;
		XIGroupState g;
		double x, y, root_x = -1,root_y = -1;
		if (XIQueryPointer(dpy,devid,root,&rw,&rc,&root_x,&root_y,&x,&y,&b,&m,&g))
		    if ((x>=minf->x && y>=minf->y && x<=minf->x2 && y<=minf->y2) == i)
			MINF_T2(o_active|o_backlight,o_active|o_backlight)
			    chBL1(2,i);
	}
}
#endif


#if _BACKLIGHT == 1
static _short fixWin(Window win,_int x,_int y,_int w,_int h){
	MINF(minf->win == win) goto found;
	return 0;
found:
	if (minf->x == x && minf->y == y && minf->width == w && minf->height == h) return 2;
	DBG("BG MoveResizeWindow");
	minf->type |= o_changed;
	oldShowPtr |= 8;
	return 1;
}

#define WIN_EVMASK VisibilityChangeMask
//		|EnterWindowMask|LeaveWindowMask|ButtonPressMask|ButtonReleaseMask
XSetWindowAttributes winattr = {
	.override_redirect = True,
	.do_not_propagate_mask = ~(long)(WIN_EVMASK),
	.event_mask = (WIN_EVMASK),
};

// bits: 1 - free if exists, 2 - low/high
static void simpleWin(_short mode) {
	Window w = minf->win;
	if ((mode&1)) {
		XDestroyWindow(dpy,w);
		w = 0;
	}
	if (!w) {
#if 0
		w = XCreateSimpleWindow(dpy, root, minf->x,minf->y,minf->width,minf->height,0,0,BlackPixel(dpy, screen));
		XChangeWindowAttributes(dpy, w, CWOverrideRedirect|CWEventMask, &winattr);
#else
		w = XCreateWindow(dpy, root, minf->x,minf->y,minf->width,minf->height, 0, 0, InputOutput, CopyFromParent, CWBackPixel|CWBorderPixel|CWOverrideRedirect|CWEventMask, &winattr);
#endif
		XISelectEvents(dpy, w, ximaskWin, Nximask);
	} else if (minf->type&o_changed) {
		XMoveResizeWindow(dpy,w,minf->x,minf->y,minf->width,minf->height);
	}
	if (mode&2) {
		XRaiseWindow(dpy,w);
	} else {
		XLowerWindow(dpy, w);
	}
#ifdef _WIN_STATE
	win = w;
	wSetProp(aWMState,XA_ATOM,(mode&2)?&aAbove:&aBelow,1,0);
	win = win1;
#endif
//	if (!minf->win)
	    XMapWindow(dpy, w);
//	DBG("window 0x%lx",w);
	minf->win = w;
}

#endif
#endif //_BACKLIGHT == 1

RROutput prim0, prim;

static void _pr_sync() {
	switch (prI) {
#if _BACKLIGHT
	    case xrp_bl:
		_bl_sysfs();
		break;
#endif
	}
}

static _short _pr_free(_short err){
	_short ret = 0;
	if (pr->en) {
		if ((err || (pi[p_safe]&2048))
		    && !XRP_EQ(pr->v,pr->vs[0])) {
			ret++;
#if 0
			if (err == 2) _pr_set(0);
			else 
#endif
			{
			
				pr->v = pr->vs[0];
				xrSetProp(a_xrp[prI],type_xrp[prI],&pr->v,1,0);
				_pr_sync();
			}
		}
		pr->en = 0;
	}
	if (pr->my) {
		XRRDeleteOutputProperty(dpy,minf->out,a_xrp[prI]);
		pr->my = 0;
		ret++;
	}
	if (err == 2 && !(pi[p_safe]&2048)) {
		XRRDeleteOutputProperty(dpy,minf->out,a_xrp_save[prI]);
		_wait_mask = 0;
		ret++;
	}
	_free(pr->p);
	return ret;
}

static void _minf_free(){
#if _BACKLIGHT == 1
	if (minf->win) {
		XDestroyWindow(dpy,minf->win);
		minf->win = 0;
	}
#endif
	_sysfs_free();
	for (prI=0; prI<xrp_cnt; prI++) {
		pr = &minf->prop[prI];
		_pr_free(0);
	}
}

static _short _pr_chk(_xrp_t *v){
#if _BACKLIGHT
	if (prI == xrp_bl && v->i == 0) return 1;
#endif
	XRRPropertyInfo *p = pr->p;
	int i;
	if (p->range) {
		if (p->num_values != 2 || type_xrp[prI] != XA_INTEGER) return 0;
		_short rr = 0;
		if (v == &val_xrp[xrp_bpc]) {
			if ((pi[p_Safe]&2) || !bits_per_rgb) return 0;
			*(prop_int*)v = bits_per_rgb;
			rr = 1;
		}
		if (*(prop_int*)v < p->values[0]) {
			if (rr) *(prop_int*)v = p->values[0];
			else return 0;
		}
		if (*(prop_int*)v > p->values[1]) {
			if (rr) *(prop_int*)v = p->values[1];
			else return 0;
		}
		return 1;
	} else if (type_xrp[prI] == XA_INTEGER) {
		for (i=0; i<p->num_values; i++) if (v->i == p->values[i]) return 1;
	} else  if (type_xrp[prI] == XA_ATOM) {
		for (i=0; i<p->num_values; i++) if (v->a == p->values[i]) return 1;
	}
	return 0;
}

static _short _pr_inf(Atom prop){
	XRRPropertyInfo *pinf = XRRQueryOutputProperty(dpy,minf->out,prop);
	if (pinf && pr->p
	    && pr->p->range == pinf->range
	    && pr->p->num_values == pinf->num_values
	    && !memcmp(pr->p->values,pinf->values,sizeof(pinf->values[0])*pinf->num_values)) {
		XFree(pinf);
		return 1;
	}
	if (prop == a_xrp[prI]) {
		if (pr->p) XFree(pr->p);
		pr->p = pinf;
		_pr_sync();
	} else if (pinf) XFree(pinf);
	return 0;
}

static void _pr_get(_short r);
static void _pr_set(_short state){
	_xrp_t v;
	v = pr->vs[state];
//	if (type_xrp[prI] == XA_ATOM && !v.a) return;
	if (xrevent1 < 0) _pr_get(0);
	if (!pr->en || XRP_EQ(v,pr->v)) return;
//	if (type_xrp[prI] == XA_ATOM) DBG("set %s %s -> %s",natom(0,minf->name),natom(1,pr->v.a),natom(2,v.a));
	pr->v1 = pr->v;
	pr->v = v;
	_pr_sync();
	xrSetProp(a_xrp[prI],type_xrp[prI],&v,1,0);
}

static void pr_set(xrp_t prop,_short state){
	pr = &minf->prop[prI = prop];
	if (pr->en)
	    _pr_set(state);
}

// 0=flush+read, 1=faster read, 2=info, 3=info+saved
static void _pr_get(_short r){
	switch (prI) {
	    case xrp_bpc:
		if ((pi[p_Safe]&2) || !bits_per_rgb) return;
	    case xrp_non_desktop:
		break;
#if _BACKLIGHT
	    case xrp_bl:
		if (!(pi[p_safe]&4096)) return;
		break;
#endif
#ifdef  XSS
	    case xrp_rgb:
		if ((pi[p_Safe]&4) || bits_per_rgb > 8) return;
#endif

	    default:
		if (!val_xrp[prI].a) return;
	    	break;
	}
	Atom prop = a_xrp[prI];
	Atom save = a_xrp_save[prI];
	Atom type = type_xrp[prI];
	_short _save = !pr->my && !(pi[p_safe]&2048);
	_short _init = (_wait_mask&_w_init) && !pr->en;
	_xrp_t v;
	if (!r) _xrPropFlush();
	if (!xrGetProp(prop,type,&v,1,0)) goto err;
//	if (type == XA_ATOM) DBG("get %s %s = %s",natom(0,minf->name),natom(1,prop),natom(2,v.a));
//	if (type == XA_INTEGER) DBG("get %s %s = %i",natom(0,minf->name),natom(1,prop),v.i);
	_short inf = (pr->p && r==1)?:_pr_inf(prop);
	if (inf && pr->en && r != 2) {
		// new default/saved or nothing new
		if (XRP_EQ(v,pr->v)) {
			pr->v1 = v;
			return;
		}
		// old?
		if (XRP_EQ(v,pr->v1)) goto _sync; 
		pr->v1 = pr->v = v;
		if (!_pr_chk(&v)) goto err;
		pr->vs[0] = v;
		if (_save)
		    xrSetProp(save,type,&v,1,0); // MUST be ok
_sync:
		_pr_sync();
		return;
	}
	pr->v = v;
	if (!pr->p || pr->p->num_values < 2 || !_pr_chk(&val_xrp[prI]) || !_pr_chk(&v)) goto err;
	if (!pr->en) {
		pr->vs[0] = pr->v1 = v;
		pr->vs[1] = val_xrp[prI];
		switch (prI) {
#if _BACKLIGHT
		    case xrp_bl:
			pr->vs[1].i = pr->p->values[0];
			break;
#endif
		    default:
			pr->vs[1] = val_xrp[prI];
			break;
		}
	}
	if (!inf) {
		if (!_pr_chk(&pr->vs[1])) goto err;
	}
	_short saved = 0;
	pr_t = type;
	if (r != 2 && (_save || _init) && xrGetProp(save,type,&v,1,0)) {
		pr->vs[0] = v;
		saved = 1;
	}
	switch (prI) {
#if _BACKLIGHT
	    case xrp_bl: // prevent to saved Backlight 0
		if (pr->vs[0].i == pr->p->values[0]) pr->vs[0].i = pr->p->values[1];
		break;
#endif
	}
	if (!_pr_chk(&pr->vs[0])) pr->vs[0] = pr->v;
	if (_init && saved && !XRP_EQ(pr->vs[0],pr->v)) {
		xrSetProp(a_xrp[prI],type,&pr->vs[0],1,0);
		pr->v1 = pr->vs[0];
	}
	if (r != 2 && ((!_save && _init) || pr_t != type || !_pr_inf(save))) {
		 XRRDeleteOutputProperty(dpy,minf->out,save);
		 saved = 0;
	}
	if (!_save || (saved && XRP_EQ(v,pr->vs[0]))) goto ok;
	if (!saved) {
		xrProp_ch = 1;
		XRRConfigureOutputProperty(dpy,minf->out,save,False,pr->p->range,pr->p->num_values,pr->p->values);
	}
	if (!xrSetProp(save,type,&pr->vs[0],1,0x10)) goto err;
ok:
	pr->en = 1;
	return;
err:
	_pr_free(1);
}

static void _scr_size();
static _short xrEvent() {
	if (ev.type == xrevent) {
#undef e
#define e ((XRRScreenChangeNotifyEvent*)&ev)
		// RANDR time(s) is static
		//TIME(T,e->timestamp > e->config_timestamp ? e->timestamp : e->config_timestamp);
		oldShowPtr |= 8|16;
		if (xrevent1 < 0 || minf0.rotation != e->rotation) {
			minf0.rotation = e->rotation;
			oldShowPtr |= 8|16;
		}
		if (XRRUpdateConfiguration(&ev)) _scr_size();
	}
#ifdef RROutputPropertyNotifyMask
	    else if (ev.type == xrevent1) {
#undef e
#define e ((XRRNotifyEvent*)&ev)
		// RANDR time(s) is static
		//XRRTimes(dpy,screen,&T);
		oldShowPtr |= 16;
#ifndef MINIMAL
		switch (e->subtype) {
#undef e
#define e ((XRROutputPropertyNotifyEvent*)&ev)
		    case RRNotify_OutputProperty:
			pr = NULL;
			MINF(minf->out == e->output) {
				for (prI = 0; prI<xrp_cnt; prI++) {
					if (e->property == a_xrp_save[prI]) return 1;
					if (e->property != a_xrp[prI]) continue;
					if (e->state == PropertyDelete || !minf->prop[prI].en) {
						oldShowPtr |= 8;
						DBG("%s xr prop '%s' on %s",e->state == PropertyDelete?"delete":"new",natom(0,minf->name),an_xrp[prI]);
						_xrPropFlush();
						return 1;
					}
					if (prI == xrp_non_desktop) oldShowPtr |= 8;
					pr = &minf->prop[prI];
					break;
				}
				break;
			}
			if (!pr) return 1;
#if 1
			// speculative check
			_xrp_t v = pr->v;
			// our waited change
			if (!XRP_EQ(v,pr->v1)) {
				pr->v1 = v;
				return 1;
			}
			// fast path ok & new value
			_pr_get(1);
			if (pr->en && !XRP_EQ(v,pr->v)) return 1;
#endif
			_pr_get(0);
			xrPropFlush();
			break;
		    default:
			oldShowPtr |= 8;
		}
#else
		oldShowPtr |= 8;
#endif
	}
#endif
	else return 0;
	return 1;
}

static void clEvents(int event){
	if (event > 0) while (XCheckTypedEvent(dpy,event,&ev)) {
		xrEvent();
	}
}

static void getBPC(){
	Visual *v = DefaultVisual(dpy,screen);
	bits_per_rgb = v ? v->bits_per_rgb : 0;
}

static void fixGeometry();
static void xrMons0(){
    if (!xrr) {
	minf_last = outputs = &minf0;
	minf_last++;
	noutput = 1;
	oldShowPtr |= 16;
	minf0.type = o_active;
	if (minf0.width && minf0.height) minf0.type|=o_size;
	if (minf0.mwidth && minf0.mheight) minf0.type|=o_msize;
	return;
    }
    unsigned long i;
    xrrFree();
    _grabX();
//    xrrSet();
    XSync(dpy,False);
    clEvents(xrevent);
    clEvents(xrevent1);
    xrrSet();
    oldShowPtr &= ~8;
    int nout = -1, active = 0, non_desktop = 0, non_desktop0 = 0, non_desktop1 = 0;
#if _BACKLIGHT
    backlight = 0;
#endif
    XRRCrtcInfo *cinf = NULL;
    XRROutputInfo *oinf = NULL;
    i = xrrr ? xrrr->noutput : 0;
    if (noutput != i) {
	if (outputs) {
		for(minf=outputs; (minf!=minf_last); minf++) _minf_free();
		free(outputs);
	}
	prim0 = prim = 0;
	minf2 = minf1 =
	outputs = minf_last = NULL;
	noutput = i;
    }
    if (!noutput) goto ret;
    if (!outputs) {
	outputs = calloc(noutput,sizeof(minf_t));
	minf_last = &outputs[noutput];
	oldShowPtr |= 16;
    }
    prim0 = prim = XRRGetOutputPrimary(dpy, root);
    if ((pi[p_safe]&128)) {
		pi[p_safe]=(pi[p_safe]|(32|64)) ^ (prim?32:64);
		prim0 = 0;
    }
    if (!(pi[p_safe]&32)) prim0 = 0;
    // getBPC();
    while(1) {
	if (cinf) {
		XRRFreeCrtcInfo(cinf);
		cinf = NULL;
	}
	if (oinf) {
		XRRFreeOutputInfo(oinf);
		oinf = NULL;
	}
	while (++nout<xrrr->noutput) {
		minf = &outputs[nout];
		minf->crt = 0;
		minf->type = 0;
		RROutput o = xrrr->outputs[nout];
		if (minf->out != o) {
			_minf_free();
			if (minf->out || !o) {
				memset(minf,0,sizeof(minf_t));
			}
			minf->type = o_changed;
			minf->out = o;
		}
		if (!o) continue;
//		minf->type |= o_out;
		if (o == prim) minf->type |= o_primary;
		if (oinf = XRRGetOutputInfo(dpy, xrrr, o)) break;
	}
	if (!oinf) break;
	if ((minf->connection = oinf->connection) == RR_Connected) {
		minf->type |= o_active;
		active++;
	}
	minf->name = XInternAtom(dpy, oinf->name, False);
	if (minf->mwidth != oinf->mm_width || minf->mheight != oinf->mm_height) {
		if (minf->mwidth1 != oinf->mm_width || minf->mheight1 != oinf->mm_height) minf->type |= o_changed;
		minf->mwidth1 = minf->mwidth = oinf->mm_width;
		minf->mheight1 = minf->mheight = oinf->mm_height;
	}
	_int mwh25 = 0;
	if (minf->mwidth && minf->mheight) {
		minf->type |= o_msize;
		mwh25 = minf->mwidth*25/minf->mheight;
	}

	XRRModeInfo *m,*m0 = NULL,*m1 = NULL, *m0x = NULL;
	RRMode id;
	int j;
	// find first preferred simple: m0
	// and with maximal size|clock: m0x
	for (j=0; j<(oinf->npreferred?:oinf->nmode); j++) {
		id = oinf->modes[j];
		for (i=0; i<xrrr->nmode; i++) {
			m = &xrrr->modes[i];
			if (m->id != id) continue;
			if (m->width && m->height) {
				if (!m0x || ((m->width == m0x->width && m->height == m0x->height)?
				    (m->dotClock > m0x->dotClock):
				    (m->width >= m0x->width && m->height >= m0x->height)))
					    m0x = m;
			} else if (!m0) m0 = m;
		}
	}
	m = m0x;
	if (!m0x) m = m0;
	else if (mwh25 && mwh25 != m0x->width*25/m0x->height)
		DBG("WARNING: output %s has maximum [preferred] mode %ux%u not proportional to size in mm",oinf->name,m0x->width,m0x->height);
	if (!(pi[p_safe]&64)
	    && !oinf->crtc
	    && m
	    && minf->connection == RR_Connected
	    ) {
		for (i=0; i<oinf->ncrtc; i++) {
			cinf=XRRGetCrtcInfo(dpy, xrrr, oinf->crtcs[i]);
			if (!cinf->noutput && !cinf->mode)
			    for (j=0; j<cinf->npossible; j++) {
				if (cinf->possible[j] == minf->out) {
					// scr size unbalanced here. start on top
					DBG("output %s off -> auto crtc %ix%i",oinf->name,m->width,m->height);
					if (XRRSetCrtcConfig(dpy,xrrr,oinf->crtcs[i],CurrentTime,0,0,m->id,RR_Rotate_0,&minf->out,1)==Success) {
						XRRFreeOutputInfo(oinf);
						minf->type |= o_changed;
						oinf = XRRGetOutputInfo(dpy, xrrr, minf->out);
						if (!oinf || oinf->crtc) break;
					}
				}
			}
			XRRFreeCrtcInfo(cinf);
			cinf = NULL;
			if (!oinf || oinf->crtc) break;
		}
_on:
		if (!oinf) continue;
	}

#if _BACKLIGHT
	_mon_sysfs_name = oinf->name;
	_mon_sysfs_name_len = oinf->nameLen;
#endif
	int nprop = 0;
	Atom *props = XRRListOutputProperties(dpy, minf->out, &nprop);
	for (prI=0; prI<xrp_cnt; prI++) {
		_short r = 0;
		for (i=0; i<nprop; i++) {
			if (a_xrp[prI] == props[i]) r|=2;
			else if (a_xrp_save[prI] == props[i]) r|=1;
		}
		if (r<2) continue;
		pr = &minf->prop[prI];
#ifndef MINIMAL
		if (pr->en) continue;
#endif
		_pr_get(r);
	}
	XFree(props);
	if (minf->prop[xrp_non_desktop].en) {
		if (minf->prop[xrp_non_desktop].v.i == 1) {
			minf->type |= o_non_desktop;
			non_desktop++;
		}
		if (minf->prop[xrp_non_desktop].vs[0].i == 1) {
			non_desktop0++;
			if ((minf->type&o_active)) active--; // think there are not active-born
			if (!minf->prop[xrp_non_desktop].v.i) non_desktop1++;
		}
	}
#if _BACKLIGHT
	pr = &minf->prop[prI = xrp_bl];
	if (pr->en) {
		minf->type |= o_backlight;
		if (!backlight && (pi[p_safe]&4096)) {
			backlight = 1;
			oldShowPtr |= 32|64;
		}
	} else if (_mon_sysfs_name) {
#ifdef SYSFS_CACHE
		// force
		//if (inf->blfd == -1)
		    minf->blfd = 0;
#endif
		//_pr_sync();
		_bl_sysfs();
	}
	_mon_sysfs_name = NULL;
#endif
	if (!(minf->crt = oinf->crtc) || !(cinf=XRRGetCrtcInfo(dpy, xrrr, minf->crt))) continue;
	minf->width = minf->width0 = cinf->width;
	minf->height = minf->height0 = cinf->height;
	if (minf->x != cinf->x || minf->y != cinf->y || minf->rotation != cinf->rotation) {
		minf->x = cinf->x;
		minf->y = cinf->y;
		minf->rotation = cinf->rotation;
		minf->type |= o_changed;
	}
	minf->mode = id = cinf->mode;
	// find precise current mode
	for (i=0; i<xrrr->nmode; i++) {
		m = &xrrr->modes[i];
		if (m->id == id) {
			m1 = m;
			break;
		}
	}
	if (pi[p_safe]&256) m0 = m1;
	else if (m0x) m0 = m0x;
	if (m1) {
		minf->width = m1->width;
		minf->height = m1->height;
	}
	if (!(pi[p_safe]&64))
		pr_set(xrp_sm,m1 && mwh25 && minf->height && mwh25 == minf->width*25/minf->height);
	if (m0) {
		minf->width0 = m0->width;
		minf->height0 = m0->height;
	}
	minf->width1 = minf->width0;
	minf->height1 = minf->height0;
	if ((minf->r90 = !!(minf->rotation & (RR_Rotate_90|RR_Rotate_270)))) {
		i = minf->width;
		minf->width = minf->height;
		minf->height = i;
		i = minf->width0;
		minf->width0 = minf->height0;
		minf->height0 = i;
		i = minf->mwidth;
		minf->mwidth = minf->mheight;
		minf->mheight = i;
	}
	_int x2 = minf->x + minf->width - 1;
	_int y2 = minf->y + minf->height - 1;
	if (x2 != minf->x2 || y2 != minf->y2) {
		minf->x2 = x2;
		minf->y2 = y2;
		minf->type |= o_changed;
	}
	if (minf->width && minf->height) minf->type |= o_size;
    }
    MINF_T(o_changed) {
	oldShowPtr |= 16|32;
	DINF(dinf->mon && dinf->mon == minf->name) dinf->type |= o_changed;
	if (!(pi[p_Safe]&2) && bits_per_rgb) pr_set(xrp_bpc,1);
    }
    if (!active && non_desktop)
	MINF_T(o_non_desktop) pr_set(xrp_non_desktop,1);
    if (active && non_desktop1)
	MINF(minf->prop[xrp_non_desktop].en && minf->prop[xrp_non_desktop].vs[0].i == 1)
	    pr_set(xrp_non_desktop,0);
#if _BACKLIGHT == 1
    if ((backlight)) {
	MINF((minf->type & (o_active|o_backlight)) == (o_active|o_backlight) && minf->width && minf->height) {
		if (!minf->win || (minf->type|o_changed)) {
			simpleWin(0);
			oldShowPtr |= 16;
		}
    } else if (minf->win) {
		XDestroyWindow(dpy,minf->win);
		minf->win = 0;
		oldShowPtr |= 16;
    }}
#endif
ret: {}
//    if (_y > minf0.height) fixGeometry();
//    _ungrabX(); // forceUngrab() instead
}

//#define __MIN_PIXELS
static inline void _round_dpi(_int *w,_int *m,double dpm) {
	if (!(dpm > 0)) return;
#ifdef MINIMAL
	*m = (*w)/dpm + .5;
	if (_DPI_DIFF < 0 && !(pi[p_safe]&(16|512)) && !(pi[p_Safe]&1)) {
		// adding some virtual pixels make DPI rounding better
		_int x;
#ifdef __MIN_PIXELS
		if ((x = dpm*(*m)) > *w || ++x > *w) *m = (*w = x)/dpm + .5;
#else
		if ((x = dpm*(*m) + .5) > *w) *m = (*w = x)/dpm + .5;
#endif
	}
#else
	*m = (*w)/dpm;
	_int w1 = *w, m1 = *m+1;
	double d = w1/(*m+0.) - dpm, d1 = dpm - w1/(*m+1.);
	if (_DPI_DIFF < 0 && !(pi[p_safe]&(16|512)) && !(pi[p_Safe]&1)) {
		// adding some virtual pixels make DPI rounding better
		_int w2 = dpm*m1;
		double d2 = dpm - (w2+0.)/m1;
		if (d2 < 0) d2 = -d2;
		if (d2 < d1) {
			d1 = d2;
			w1 = w2;
		} 
#ifdef __MIN_PIXELS
		    else
#endif
		{
			d2 = dpm - (++w2+0.)/m1;
			if (d2 < 0) d2 = -d2;
			if (d2 < d1) {
				d1 = d2;
				w1 = w2;
			}
		}
	}
	if (d1 < d) {
		*m = m1;
		*w = w1;
		// d = d1;
	}
#endif
}

// mode=1 - preset, mode=2 - force, 3 - before mon init
static _short setScrSize(double dpmw,double dpmh,_short mode,_int px,_int py1){
	_int py=0, mpx = 0, mpy = 0;
	if (mode == 3) {
		px = _max(px,w_width);
		py = w_height;
		if (w_mwidth && w_mheight) {
			mpx = w_mwidth;
			mpy = w_mheight;
		}
		if (minf->mwidth || minf->mheight) {
			mpx = _max(minf->mwidth?:(minf->mheight*px/py1),mpx);
			mpy = _max(minf->mheight?:(minf->mwidth*py1/px),mpy);
		}
	} else MINF_T(o_active) {
		px = _max(minf->x + minf->width, px);
		py1 = _max(minf->y + minf->height, py1);
		mpx = _max(minf->mwidth,mpx);
		mpy += minf->mheight;
		if (mode==1) {
			px = _max(minf->x + minf->width1, px);
			py += _max(minf->height,minf->height1);
			py1 = _max(minf->y + minf->height1, py1);
		} else {
			py += minf->height;
		}
	}
	py = _max(py,py1);
#ifndef MINIMAL
	int minW,minH,maxW,maxH;
	if (XRRGetScreenSizeRange(dpy,root,&minW,&minH,&maxW,&maxH)) {
		if (px < minW) px = minW;
		else if (px > maxW) px = maxW;
		if (py < minH) py = minH;
		else if (py > maxH) py = maxH;
	}
#endif
	if ((pi[p_Safe]&1)) {
		if (px < w_width) px = w_width;
		if (py < w_height) py = w_height;
	}
	if (mode == 1 && w_width >= px && w_height >= py) {
		if (minf0.width >= px && minf0.height >= py) return 0;
		// reduce preset repeats
		oldShowPtr |= 8;
	}
	if ((dpmw == 0 || dpmh == 0) && (!(pi[p_safe]&16) || !mpx || !mpy)) {
		dpmw = w_dpmw;
		dpmh = w_dpmh;
	} else DPI_EQ(dpmh,dpmw); // keep w_dpmw, w_dpmh always compared
	_round_dpi(&px,&mpx,dpmw);
	_round_dpi(&py,&mpy,dpmh);
	// check both - event-confirmed (vs. last) and last (vs. silent failure) sizes
	// set twice if unsure
	if (px != w_width || py != w_height
	    || xrevent1 < 0
	    || mode == 1 // checked
	    || (
		    px != minf0.width || py != minf0.height
		    || mode == 2
		    || mode == 3
		    || _DPI_CH(dpm0w,dpm0h,dpmw,dpmh,mpx != minf0.mwidth || mpy != minf0.mheight)
		    || _DPI_CH(dpmw,dpmh,w_dpmw,w_dpmh,mpx != w_mwidth || mpy != w_mheight)
		)
		) {
		DBG("set screen size(%i) "fint"x"fint" / "fint"x"fint"mm - %.1f %.1fdpi",mode,px,py,mpx,mpy,INCH*px/mpx,INCH*py/mpy);
#ifndef MINIMAL
		XFlush(dpy);
		XSync(dpy,False);
		_error = 0;
#endif
		XRRSetScreenSize(dpy,root,px,py,mpx,mpy);
#ifndef MINIMAL
		XFlush(dpy);
		XSync(dpy,False);
		if (_error) return 0;
#endif
		w_width = px;
		w_height = py;
		w_mwidth = mpx;
		w_mheight = mpy;
		_wait_mask |= _w_screen;
		if (dpmw > 0 && dpmh > 0) {
			w_dpmw = dpmw;
			w_dpmh = dpmh;
		} else {
			w_dpmw = (.0+w_width)/w_mwidth;
			w_dpmh = (.0+w_height)/w_mheight;
		}
		return 1;
	}
	return 0;
}

_int pan_x,pan_y,pan_cnt;
static void _pan(minf_t *m) {
	if (pi[p_safe]&64 || !m || !m->crt) return;
	_short ch = 0;
	if (pi[p_safe]&16384) {
		if (m->x != 0 || m->y != pan_y) {
			DBG("XRRSetCrtcConfig() move crtc %lu "fint","fint" -> "fint","fint"",m->crt,m->x,m->y,(_int)0,pan_y);
			if (XRRSetCrtcConfig(dpy,xrrr,m->crt,CurrentTime,0,pan_y,m->mode,m->rotation,&m->out,1)==Success) {
				ch = 1;
				//XFlush(dpy);
			}
			//else goto pan0;
		}
//		goto pan1;
	}
pan0:
	XRRPanning *p = XRRGetPanning(dpy,xrrr,m->crt), p1;
	if (!p) return;
	memset(&p1,0,sizeof(p1));
	p1.top = pan_y;
	p1.width = m->width1;
	p1.height = m->height1;
#if 1
	p1.timestamp = p->timestamp;
	if (memcmp(&p1,p,sizeof(p1))) {
		p1.timestamp = CurrentTime;
#else
	if (p1.width != p->width || p1.height != p->height || p1.top != p->top || (p->left|p->track_width|p->track_height|p->track_left|p->track_top|p->border_left|p->border_top|p->border_right|p->border_bottom)) {
#endif
		DBG("crtc %lu panning %ix%i+%i+%i/track:%ix%i+%i+%i/border:%i/%i/%i/%i -> %ix%i+%i+%i/{0}",
			m->crt,p->width,p->height,p->left,p->top,  p->track_width,p->track_height,p->track_left,p->track_top,  p->border_left,p->border_top,p->border_right,p->border_bottom,
			p1.width,p1.height,p1.left,p1.top);
		if (XRRSetPanning(dpy,xrrr,m->crt,&p1)==Success) ch = 1;
	}
	XRRFreePanning(p);
pan1:
	pan_x = _max(pan_x, m->width);
	pan_y += m->height;
	if (ch) {
		pan_cnt++;
		m->type |= o_changed;
		XFlush(dpy);
		XSync(dpy,False);
	}
}


#ifdef XSS
static void _monFS(xrp_t p, _short st){
	if (val_xrp[p].a) {
		pr_set(p,st);
		xrPropFlush();
	}
}

static void monFullScreen(){
	if (!val_xrp[xrp_ct].a && !val_xrp[xrp_cs].a && !val_xrp[xrp_rgb].a) return;
	if (noXSS) {
		wa.x = -1;
		wa.y = -1;
		getGeometry();
	}
	MINF(minf->out) {
		_short st = noXSS && (minf->type&o_active) && wa.x>=minf->x && wa.x<=minf->x2 && wa.y>=minf->y && wa.y<=minf->y2;
		_monFS(xrp_ct,st);
		_monFS(xrp_cs,st);
		_monFS(xrp_rgb,st);
	}
//	xrPropFlush();
}
#endif

static _short findResDev(){
	if (resDev) DINF(dinf->devid == resDev) {
		dinf2 = dinf;
		if (!dinf->mon) return 2;
		MINF(minf->name == dinf->mon) {
			if (minf2 == minf) return 1;
			minf2 = minf;
			return 0;
		}
		minf2 = NULL;
		return 1;
	}
	minf2 = NULL;
	dinf2 = NULL;
	return 0;
}

static void fixGeometry(){
	findResDev();
	if (minf2) goto find1;
	DINF(TDIRECT(dinf->type)) {
		MINF((minf->type&o_active) && dinf->mon == minf->name) {
			minf2 = minf;
			goto find1;
		}
	}
find1:
	minf1 = NULL;
	_uint nm = 0;
	MINF_T(o_active) {
		nm++;
		if (!minf2) DINF(dinf->mon == minf->name){
			minf2 = minf;
			break;
		};
		if ((prim0 == minf->out) || (!prim0 && (!minf1 || minf->mheight > minf1->mheight)))
		    minf1 = minf;
	}
	if (!minf2) minf2 = &minf0;
	if (pf[p_res]<0) {
		if (!minf2->mwidth && !minf2->mheight) {
			resX = resY = -pf[p_res];
			ERR("Screen dimensions in mm unknown. Use resolution in dots.");
		} else {
			if (minf2->mwidth) resX = (0.+minf0.width)/minf2->mwidth*(-pf[p_res]);
			resY = minf2->mheight ? (0.+minf0.height)/minf2->mheight*(-pf[p_res]) : resX;
			if (!minf2->mwidth) resX = resY;
		}
	}
	if (!minf1 || !xrr) return;
	if (!(pi[p_safe]&32) && minf1 && minf1->out != prim) {
		DBG("primary output %lu",minf1->out);
		XRRSetOutputPrimary(dpy,root,prim = minf1->out);
	}
	_short do_dpi = minf1 && minf1->mheight && minf1->mwidth && !(pi[p_safe]&16);
	if ((do_dpi || !(pi[p_safe]&64))) {
		double dpmh = pi[p_dpi] ? pf[p_dpi] : (.0 + minf1->height) / minf1->mheight, dpmw = pi[p_dpi] ? pf[p_dpi] : (.0 + minf1->width) / minf1->mwidth;
		pan_x = pan_y = pan_cnt = 0;
		if (do_dpi) MINF_T(o_active) {
			if (minf1 && minf1->crt == minf->crt) continue;
			if (minf2 && minf2->crt == minf->crt) continue;
			if (minf->mwidth && minf->mheight) {
				if ( !(minf1 && minf->width == minf1->width && minf->height == minf1->height && minf->mwidth == minf1->mwidth && minf->mheight == minf1->mheight) &&
				     !(minf2 && minf->width == minf2->width && minf->height == minf2->height && minf->mwidth == minf2->mwidth && minf->mheight == minf2->mheight))
					fixMonSize(minf, &dpmw, &dpmh);
			}
		}

//		if (!(pi[p_safe]&64) && setScrSize(dpmw,dpmh,1,0,0)) goto ex;
		if (!(pi[p_safe]&64)) {
			_wait_mask |= _w_pan;
			if (setScrSize(dpmw,dpmh,1,0,0)) goto ex1;
			_wait_mask &= ~_w_pan;
			_grabX();

			if (minf1 && minf1->crt != minf2->crt) _pan(minf1);
			MINF_T(o_active) {
				if (minf1 && minf1->crt == minf->crt) continue;
				if (minf2 && minf2->crt == minf->crt) continue;
				_pan(minf);
			}
			_pan(minf2);
		}

//		xrPropFlush();

		if (!pan_x) pan_x = minf0.width;
		if (!pan_y) pan_y = minf0.height;

		if (do_dpi) {
			if (minf2->crt && minf1->crt != minf2->crt && minf2->crt) fixMonSize(minf2, &dpmw, &dpmh);
			fixMonSize(minf1, &dpmw, &dpmh);
			if (setScrSize(dpmw,dpmh,0,0,0)) goto ex1;
		}
		if (!pan_cnt) goto ex1;
		// workaround "panning event": force other apps to change geometry
		_set_same_size();
ex1:
		if (!(pi[p_safe]&64)) _ungrabX();
	}
ex:
	xrrFree();
	if (pan_cnt) oldShowPtr |= 8|16;
}

_short busy;
int evcount;
static Bool filterTouch(Display *dpy1, XEvent *ev1, XPointer arg){
	if (dpy1 == dpy && ev1->type == GenericEvent && ev1->xcookie.extension == xiopcode)
	    switch (ev1->xcookie.evtype) {
	    case XI_TouchBegin:
//	    case XI_TouchUpdate:
//	    case XI_TouchEnd:
	    case XI_ButtonPress:
//	    case XI_ButtonRelease:
#if 0
		busy = 1;
#else
		if (!XGetEventData(dpy, &ev1->xcookie)) break;
		if (devid == ((XIDeviceEvent*)ev1->xcookie.data)->deviceid) busy = 1;
                XFreeEventData(dpy, &ev1->xcookie);
#endif
	}
	return busy || (evcount-- > 0);
}

int xtestPtr, xtestPtr0;
_uint tdevs = 0, tdevs2=0;
//#define floating (pi[p_floating]==1)


_uint ninput1_;
XIDeviceInfo *d2;
// remember only devices on demand
static void _add_input(_short t){
	if (dinf1) goto ex;
	_uint i = ninput1_++;
	if (ninput1_ > ninput1 || !inputs1) {
		_uint n = i + NINPUT_STEP;
		dinf_t *p = malloc(n*sizeof(dinf_t));
		if (inputs1) {
			memcpy(p,inputs1,i*sizeof(dinf_t));
			free(inputs1);
		}
		memset(&p[i],0,NINPUT_STEP*sizeof(dinf_t));
		inputs1 = p;
		ninput1 = n;
#ifndef MINIMAL
		// some overcoding to non-iterated way
		dinf = inputs;
#endif
	}
	dinf1 = &inputs1[i];
#ifndef MINIMAL
	// second scans for multiple devices
	if (dinf != dinf_last && dinf->devid == devid) goto found;
#endif
	DINF(dinf->devid == devid) {
#ifndef MINIMAL
found:
#endif
		*dinf1 = *dinf;
#ifndef MINIMAL
		dinf++;
#endif
		dinf1->type = t|(dinf1->type&o_changed); // reset
		if (memcmp(&dinf1->xABS,&xABS,sizeof(xABS))) goto ch;
		return;
	}
#ifndef MINIMAL
	dinf = inputs;
#endif
	dinf1->devid = devid;
	dinf1->attach0 = d2->attachment;
	dinf1->mindiffZ = 2;
ch:
	t |= o_changed;
	memcpy(&dinf1->xABS,&xABS,sizeof(xABS));
	// XI_DeviceChanged==1 set everywhere, so 0 is "no event set"
//	dinf1->evmask[0] = ...:
ex:
	dinf1->type |= t;
}

static void _reason(char *s){
	DBG("input %i == output %s: %s",dinf->devid,dinf->mon?natom(0,dinf->mon):"none",s);
}

static _short _REASON(Atom m,_short r,char *txt){
	if (!m) return 0;
	dinf->reason=r;
	if (dinf->mon!=m) {
		dinf->mon=m;
		dinf->type |= o_changed;
		_reason(txt);
	}
	return 1;
}

static void touchToMon(){
	minf = minf_last;
	DINF((dinf->type&o_absolute) && (dinf->reason || !dinf->mon)) {
		Atom m = 0;
		if (_REASON(mon,0,"by command line")) continue;
		devid = dinf->devid;
		xiGetProp(aDevMon,XA_ATOM,&m,1,0);
		if (_REASON(m,0,"by xinput property")) continue;
		int n = 0, n0 = 0;
		Atom m0 = 0;
		MINF_T(o_size|o_msize) {
			if (!(minf->type&o_msize)) {
				if (_max(minf->width,minf->height)*10/_min(minf->width,minf->height) == _max(dinf->ABS[0],dinf->ABS[1])*10/_min(dinf->ABS[0],dinf->ABS[1])) {
					if (!n0++) m0 = minf->name;
				}
			} else if (abs(dinf->ABS[minf->r90] - minf->mwidth) < mon_grow && abs(dinf->ABS[!minf->r90] - minf->mheight) < mon_grow) {
				if (!n++) m = minf->name;
			}
		}
		if (!n && n0 == 1) {
			_REASON(m0,2,"\"zero mm size\" monitor by proportions");
		}
		if (n == 1 && _REASON(m,1,"by size")) continue;
		if (dinf->reason<2 && _REASON(dinf->mon,dinf->reason,"")) continue;
		// unprecise methods now
		if (_REASON(m0,3,"\"zero mm size\" monitor by proportions first")) continue;
		if (_REASON(m,3,"by size first")) continue;
		_REASON(dinf->mon,dinf->reason,"");
	}

	DINF(dinf->mon) MINF(minf->name == dinf->mon) {
		if (!(dinf->ABS[0]>.0 && dinf->ABS[1]>.0)) {
			if (minf->type&o_msize) {
				_reason("input size from monitor");
				dinf->ABS[0] = minf->mwidth1;
				dinf->ABS[1] = minf->mheight1;
				dinf->type |= o_changed;
			} //else break;
		} else if (!(minf->type&o_msize)) {
			_reason("monitor size from device (broken video driver or monitor)");
			minf->mwidth = dinf->ABS[!minf->r90] + .5;
			minf->mheight = dinf->ABS[minf->r90] + .5;
			minf->mwidth1 = dinf->ABS[0] + .5;
			minf->mheight1 = dinf->ABS[1] + .5;
			//minf->type |= o_changed;
			dinf->type |= o_changed;
		}
		if (!((dinf->type|minf->type)&o_changed)) break;
		if ((minf->type&o_changed)) {
			minf->type ^= o_changed;
			dinf->type |= o_changed;
		}
		map_to();
		break;
	}
#ifdef FAST_VALUATORS
	// valuator[i]/fastABS[i]
	DINF(dinf->type&o_changed) {
		_short i;
		dinf->type ^= o_changed;
		dinf->fast = 0;
		for (i = 0; i < 3; i++) {
			dinf->fastABS[i] = 1;
			if (!dinf->xABS[i].en) continue;
			if ((dinf->type&o_absolute)) {
				if (dinf->xABS[i].max_min > 0.
				    && dinf->xABS[i].min == 0.
				    //&& !dinf->xABS[i].resolution
				    ) {
					dinf->fastABS[i] = dinf->xABS[i].max_min / ((i==0)?minf0.width:(i==1)?minf0.height:99);
					dinf->fast |= (1 << i);
				}
			} else if (dinf->xABS[i].max_min == 0.) {
				dinf->fast |= (1 << i);
			} else dinf->fast |= (1 << (i+4));
		}
	}
#endif
	MINF(minf->type&o_changed) minf->type ^= o_changed;
}

// try libevdev/kernel labeled valuators first
// first try "Abs MT *", next "Abs *", last - first 3
typedef struct _abs_cl_t {
	_short m;
	void *cl[3];
	Atom a[3];
	unsigned char *name[5];
} abs_cl_t;
#define ABS_CL0 2
#define ABS_CL (ABS_CL0 + 1)
abs_cl_t *_abs1, _abs[ABS_CL] = {
	{.name = {"Abs MT Position X","Abs MT Position Y","Abs MT Pressure"}},
	{.name = {"Abs X","Abs Y","Abs Pressure","Abs Z"}},
};

_short _classes_unsafe;
static _short xiClasses(XIAnyClassInfo **classes, int num_classes){
	_short type1 = 0;
	int devid, j, i, k;
	memset(&xABS,0,sizeof(xABS));
	_abs[0].m = _abs[1].m = _abs[2].m = 0;
#ifdef USE_EVDEV
	XIAnyClassInfo *cl1[3];
	int ncl1[3] = {0,0,0};
#endif
	for (j=0; j<num_classes; j++) {
		XIAnyClassInfo *cl = classes[j];
		devid = cl->sourceid;
		switch (cl->type) {
#undef e
#define e ((XITouchClassInfo*)cl)
		    case XITouchClass:
			type1 |= o_touch;
			if (e->mode == XIDirectTouch) type1 |= o_directTouch;
			break;
#undef e
#define e ((XIValuatorClassInfo*)cl)
		    case XIValuatorClass:
			switch (e->mode) {
			    case Absolute:
				type1 |= o_absolute;;
				for (i=0; i<ABS_CL; i++) {
					_abs1 = &_abs[i];
					if (!_abs1->name[0]) break;
					for (k=0; k<4 && _abs1->name[k]; k++) {
						int k1 = k;
						if (k>2) k1 = 2;
						if (e->label == _a(_abs1->a[k1],_abs1->name[k])) {
							_abs1->m |= (1<<k1);
							_abs1->cl[k1] = cl;
							i = 3;
							break;
						}
					}
				}
				//_abs1 = &_abs[ABS_CL0];
#ifdef USE_EVDEV
				if (!dinf1) break;
				// detects:	z - libinput (if something broken)
				// 		x, y - others with resolution set
				unsigned long r = abs((e->resolution > 1000) ? (e->max - e->min)/(e->resolution/1000.) : (e->max - e->min));
				for (i=0; i<3; i++) {
					unsigned long l = abs(dinf1->ABS[i]);
					if (l && l == r) {
						cl1[i] = cl;
						ncl1[i]++;
					}
				}
#endif
				i = e->number;
				if (i>=0 && i<3) {
					_abs1->m |= (1<<i);
					_abs1->cl[i] = cl;
				}
			}
			break;
#undef e
#define e ((XIScrollClassInfo*)cl)
		    case XIScrollClass:
			type1|=o_scroll;
			break;
		}
	}
	_classes_unsafe = 0;
	if (!(type1&o_absolute)) goto ret;
	_classes_unsafe = 1;
#ifdef USE_EVDEV
	for (i=0;i<3;i++) {
		if (ncl1[i] == 1) {
			_abs[ABS_CL0].m |= (1<<i);
			_abs[ABS_CL0].cl[i] = cl1[i];
		} else if (i==2) _abs[ABS_CL0].m &= ~(1<<i);
	}
#endif
	if (_abs[0].m == 7) j = 0;
	else if (_abs[1].m == 7) j = 1;
	else if (_abs[0].m == 3) j = 0;
	else if (_abs[1].m == 3) j = 1;
	else if (_abs[0].m || _abs[1].m) goto ret;
	else if (_abs[ABS_CL0].m == 7) j = ABS_CL0;
	else if (_abs[ABS_CL0].m == 3) j = ABS_CL0;
	else  goto ret;
	_classes_unsafe = (j == ABS_CL0);
	_abs1 = &_abs[j];
	if (!(_abs[1].m&4) && ncl1[2] == 1) {
		_abs->m |= 4;
		_abs->cl[2] = cl1[2];
	}
	for (i = 0; i < 3; i++) {
		if (!(_abs1->m&(1<<i))) continue;
#undef e
#define e ((XIValuatorClassInfo*)cl)
		XIAnyClassInfo *cl = _abs1->cl[i];
		if (j == 2)
			DBG("input %i unknown %s label use valuator: %i '%s'",devid,j?(j==2)?"Pressure":"Y":"X",e->number,natom(0,e->label));
		xABS[i].min = e->min;
		xABS[i].max_min = e->max - e->min;
		xABS[i].resolution = e->resolution;
//		if (e->resolution && e->resolution < 1000) DBG("resolution<1000 of fixme: device %i  valuator %i '%s' min %f max %f resolution %i",devid,i,natom(i,e->label),e->min,e->max,e->resolution);
		xABS[i].en = 1;
//#ifdef FAST_VALUATORS
		if (e->max > e->min) {
			if (e->min == 0.
			    // && e->resolution == 0
				) xABS[i].en = 2;
		} else xABS[i].en = 3;
		xABS[i].number = e->number;
		xABS[i].np = e->number>>3;
		xABS[i].nm = 1 << (e->number & 7);
	}
ret:
	if (dinf1) memcpy(&dinf1->xABS,&xABS,sizeof(xABS));
	return type1;
}

static void getHierarchy(){
	inputs1 = NULL;
	ninput1_ = 0;
#ifndef MINIMAL
	dinf = inputs;
#endif
	
	int i,j,ndevs2,nkbd,m=0;
	void *mTouch = useRawTouch ? ximaskRaw : ximaskTouch;
	void *mButton = useRawButton ? ximaskRaw : ximaskButton;

	_grabX();
	XIDeviceInfo *info2 = XIQueryDevice(dpy, XIAllDevices, &ndevs2);
	if (pi[p_floating] != 1) {
	    for (i=0; i<ndevs2; i++) {
		d2 = &info2[i];
		devid = d2->deviceid;
		switch (d2->use) {
		    case XIMasterPointer:
			if (!cr.return_pointer) cr.return_pointer = devid;
			else if (cr.return_pointer != devid && !strncmp(d2->name,cm.name,11)) {
				m = devid;
				if (ca.new_master != m) {
					xiSetMask(mTouch,0);
//					XIUndefineCursor(dpy,m,root);
//					XIDefineCursor(dpy,m,root,None);
				}
			}
			break;
		    case XIMasterKeyboard:
			if (!cr.return_keyboard) cr.return_keyboard = devid;
			break;
		}
	    }
	    cr.deviceid = ca.new_master = m;
	}
	xtestPtr0 = xtestPtr = 0;
	tdevs2 = tdevs = 0;
	for (i=0; i<ndevs2; i++) {
		d2 = &info2[i];
		devid = d2->deviceid;
		dinf1 = NULL;
		_short type = 0, type1 = 0;
		busy = 0;

		mTouch = useRawTouch ? ximaskRaw : ximaskTouch;
		mButton = useRawButton ? ximaskRaw : ximaskButton;

		if ((_wait_mask&_w_init)) {
			XIDeleteProperty(dpy,devid,aABS);
		}
		switch (d2->use) {
		    case XIMasterPointer:
#ifdef __check_entered
			type|=o_master;
			break;
#else
			continue;
#endif
		    case XIFloatingSlave:
			type1 |= o_floating;
			break;
		    case XISlavePointer:
			if (!strstr(d2->name," XTEST ")) break;
			if (d2->attachment == cr.return_pointer) xtestPtr0 = devid;
			else if (m && d2->attachment == m) xtestPtr = devid;
			//xiSetMask(NULL,0);
			continue;
//		    case XISlaveKeyboard:
//			if (strstr(d2->name," XTEST ")) continue;
//			type1 |= o_kbd;
//			break;
		    case XIMasterKeyboard:
			// type |= o_kbd|o_master;
			if (!kbdDev) {
				kbdDev = devid;
				if (!getWProp(root,aActWin,XA_WINDOW,sizeof(Window)))
					xiSetMask(ximask0,XI_FocusIn);
			}
			continue;
		    default:
			continue;
		}
		type1 |= xiClasses(d2->classes,d2->num_classes);
#ifdef USE_EVDEV
		if (_classes_unsafe && (type1&o_absolute) && !(_wait_mask&_w_init)) {
			_add_input(type|=type1);
			// repeat with evdev ABS
			xiClasses(d2->classes,d2->num_classes);
		}
#endif
		if ((type|type1)&(o_kbd|o_master));
		else if (!xABS[1].en) {
			mTouch = ximaskTouch;
			mButton = ximaskButton;
			if ((useRawTouch || useRawButton) && ((type&type1)&o_absolute))
				ERR("xi device %i '%s' raw transformation unknown. broken drivers?",devid,d2->name);
		} else if ((useRawTouch || (useRawButton && xABS[2].en))) type |= type1;

		_short t = TDIRECT(type1);

		if (xABS[2].en && mButton == ximaskRaw && !(type1&o_touch)) t = 1;
		
		if (pi[p_floating] == 3) {
			if (type1&o_touch) {
				if (mTouch == ximaskRaw && xABS[1].en) t = 1;
			} else if (useRawTouch && useRawButton) { // 15
				if (mButton == ximaskRaw && xABS[1].en) t = 1;
			}
		}

		if (pa[p_device] && *pa[p_device]) {
			if (!strcmp(pa[p_device],d2->name) || pi[p_device] == devid) {
				t = 1;
			} else if (t) {
				t = 0;
			}
		}

		// remember all interesting and not too selective
		if (t || (type1&(o_absolute|o_touch|o_directTouch)) || xABS[2].en) type |= type1;

		void *c = NULL;
		cf.deviceid = ca.deviceid = ximask[0].deviceid = devid;
		ximask[0].mask = NULL;
		char *msg = NULL;
		switch (d2->use) {
		    case XIFloatingSlave:
			if (t) switch (pi[p_floating]) {
			    case 1:
				tdevs2++;
				// touches are grabbed with mask, button may be after SIGKILL/etc
				if (!(type&o_touch)) ximask[0].mask=ximaskButton;
				msg = "detached hook button events";
				break;
			    case 0:
				if (m) c = &ca;
				break;
			    case 2:
				tdevs2++;
				if (m) {
					c = &ca;
					ximask[0].mask=(void*)ximask0;
				} else if (type&o_touch) {
					ximask[0].mask=mTouch;
				} else {
					ximask[0].mask=mButton;
				}
				break;
			}
			break;
		    case XISlavePointer:
			if (t) {
				tdevs2++;
				switch (pi[p_floating]) {
				    case 1:
					if (type&o_touch) {
						ximask[0].mask=mTouch;
						DBG("input %i grab touch events%s",devid,(ximask[0].mask == ximaskRaw)?" raw":"");
						XIGrabDevice(dpy,devid,root,0,None,XIGrabModeSync,XIGrabModeTouch,False,ximask);
//						XIGrabTouchBegin(dpy,devid,root,0,ximask,0,NULL);
						ximask[0].mask=NULL;
					} else {
						msg = "detach (grab button events)";
						ximask[0].mask=mButton;
//						c = &cf;
					}
					break;
				    case 0:
					if (m && m != d2->attachment) c = &ca;
					break;
				    case 2:
					if (d2->attachment == cr.return_pointer) c = &cf;
					break;
				    case 3:
					if (!t) break;
					msg = "hook";
					if (type&o_touch) {
						ximask[0].mask=mTouch;
					} else {
						ximask[0].mask=mButton;
					}
				}
			}
//			else if ((type|type1)&o_touch) ximask[0].mask = (void*)(showPtr ? mTouch : ximask0);
//			else if ((type)&o_touch)       ximask[0].mask = (void*)(showPtr ? ximask0 : mTouch);
			else ximask[0].mask = (void*)(showPtr ? ximask0 : mButton);
			break;
//		    case XISlaveKeyboard:
//			nkbd++;
//		    default:
//			break;
		}

		if (type || ximask[0].mask) _add_input(type|=type1);
		if (pi[p_safe]&1) {
			if (!(c || ximask[0].mask)) continue;
			if (busy) {
busy:
				forceUngrab();
				oldShowPtr |= 4;
				//fprintf(stderr,"busy delay\n");
				continue;
			}
			for (j=P; j!=N; j=TOUCH_N(j)) if (touch[j].deviceid == devid) goto busy;

#if 0
			if (_grab != grabserial) {
				// server was relocked (getEvRes...), reread device state
				d2 = XIQueryDevice(dpy, devid, &j);
				if (!d2 || j < 1 || d2->deviceid != devid) {
					if (d2) XIFreeDeviceInfo(d2);
					d2 = &info2[i];
				}
			}
#endif
			for (j=0; j<d2->num_classes; j++) {
				XIAnyClassInfo *cl = d2->classes[j];
				switch (cl->type) {
#undef e
#define e ((XIButtonClassInfo*)cl)
				    case XIButtonClass: {
					int b;
#if 0
					for (b=0; b<e->num_buttons; b++) if (XIMaskIsSet(e->state.mask, b)) {busy = 1; break;}
#else
					unsigned char *bb = e->state.mask;
					for (b=e->num_buttons; b>0; b-=8) {
						if (*bb && (b>7 || (*bb)&((1<<(b+1))-1))) {busy = 1; break;}
						bb++;
					}
#endif
					break; }
				}
			}
			if (d2 != &info2[i]) XIFreeDeviceInfo(d2);
			if (busy) goto busy;

			XSync(dpy,False);
			if ((evcount=XPending(dpy))>0) {
				XEvent ev1;
				XPeekIfEvent(dpy,&ev1,&filterTouch,NULL);
				if (busy) goto busy;
			}
		}
		if (c) XIChangeHierarchy(dpy, c, 1);
		if (ximask[0].mask) {
			if (memcmp(&dinf1->evmask,ximask->mask,sizeof(xiMask))) {
				if (msg)
					DBG("input %i %s%s",devid,msg,(ximask[0].mask == ximaskRaw)?" raw":"");
				XISelectEvents(dpy, root, ximask, Nximask);
				memcpy(&dinf1->evmask,ximask->mask,sizeof(xiMask));
			}
		}
	}
	switch (pi[p_floating]) {
	    case 2:
		if (showPtr) {
			if (!m) break;
//			XIRemoveMasterInfo cr = {.type = XIRemoveMaster, .return_mode = XIFloating, .deviceid = m, .return_pointer = m0, .return_keyboard = k0};
			XIChangeHierarchy(dpy, (void*)&cr, 1);
		}
	    case 0:
		if (m) break;
		if (curShow) {
//			curShow = 0;
//			XFixesHideCursor(dpy, root);
//			XFlush(dpy);
			//if (!showPtr) fprintf(stderr,"hide\n");
		}
		XIChangeHierarchy(dpy, (void*)&cm, 1);
	}
	//_ungrabX();
	if (!(_wait_mask&_w_init)) forceUngrab();
	XIFreeDeviceInfo(info2);
	if (pi[p_safe]&8) XFixesShowCursor(dpy,root);
	tdevs -= tdevs2;

	if (inputs) free(inputs);
	inputs = inputs1;
	ninput = ninput1 = ninput1_;
	dinf_last = &inputs[ninput];
	dinf1 = NULL;
	findResDev();
#ifdef USE_EVDEV
	DINF((dinf->type&o_absolute) && !(dinf->ABS[0]>0) && !(dinf->ABS[1]>0) && !(dinf->ABS[2]>0)) {
		getEvRes();
		for (i=0; i<3; i++) {
			if (dinf->ABS[i] > 0 && !dinf->xABS[i].en) oldShowPtr |= 2;
		}
	}
#endif
	// drivers are unsafe, evdev first
	DINF((dinf->type&o_absolute) && !(dinf->ABS[0]>0) && !(dinf->ABS[1]>0) && dinf->xABS[1].en && dinf->xABS[1].resolution>=1000) {
		_short i;
		for (i=0;i<NdevABS;i++) {
			if (dinf->xABS[i].en && dinf->xABS[i].resolution > 1000 && dinf->xABS[0].max_min > 0) dinf->ABS[i] = (dinf->xABS[0].max_min)/(dinf->xABS[i].resolution/1000.);
		}
	}

	oldShowPtr |= 16;
	devid = 0;
}

static void _signals(void *f){
	static int sigs[] = {
		SIGINT,
		SIGABRT,
		SIGQUIT,
		SIGHUP,
		SIGTERM,
		0,
		};
	int *s;
	for(s=sigs; *s; s++) signal(*s,f);
}

static void _eXit(int sig){
	if (sig == SIGHUP) {
		oldShowPtr |= 2|8|16;
		return;
	}
	_signals(SIG_DFL);
	if (!grabcnt) XGrabServer(dpy);
	grabcnt = 0;

	MINF(minf->out) {
		for (prI=0; prI<xrp_cnt; prI++) {
			pr = &minf->prop[prI];
			if (_pr_free(2)) _wait_mask = 0;
		}
	}

	int m = 0;
	if (cr.return_pointer && ca.new_master && cr.return_pointer != ca.new_master) {
		cr.deviceid = ca.new_master;
		cr.return_mode = XIAttachToMaster;
		if (XIChangeHierarchy(dpy, (void*)&cr, 1) == Success) {
			m = ca.new_master;
			_wait_mask = 0;
		}
	}
	DINF(dinf->attach0) {
		ca.new_master = dinf->attach0;
		ca.deviceid = dinf->devid;
		if (XIChangeHierarchy(dpy, (void*)&ca, 1) == Success) _wait_mask = 0;
	}
	if (_wait_mask) exit(0);
	XFlush(dpy);
	win = win1 = root;
	noXSS = noXSS1 = 0;
}

static void setShowCursor(){
repeat0:
	static int cnt = 1;
//	if (--cnt && XPending(dpy)) return;
//	cnt = noutput + ninput + 1;
	cnt = 3;
repeat:
	if (oldShowPtr&8) {
		oldShowPtr ^= 8;
		xrMons0();
	}
	if (pi[p_floating] != 3)
		if (((oldShowPtr^showPtr)&1) ) oldShowPtr |= 2; // some device configs dynamic
	if (oldShowPtr&2) {
		oldShowPtr ^= 2;
		getHierarchy();
	}
	if (oldShowPtr&16) {
		oldShowPtr ^= 16;
		touchToMon();
		devid = 0;
		fixGeometry();
		xrPropFlush();
		_wait_mask &= ~_w_init;
	}
	if (oldShowPtr&64) {
		// reload windows list. sometimes
		oldShowPtr ^= 64;
#if _BACKLIGHT == 2 && defined(XTG)
		freeWins();
#endif
	}
	if (oldShowPtr&32) {
		oldShowPtr ^= 32;
#if _BACKLIGHT == 2 && defined(XTG)
		chBL(0,0,0,0,0,3);
#endif
	}
	// "Hide" must be first!
	// but prefer to filter error to invisible start
	if ((oldShowPtr^showPtr)&1) {
		oldShowPtr = (oldShowPtr&0xfe)|showPtr;
		if ((curShow=showPtr)) XFixesShowCursor(dpy, root);
		else XFixesHideCursor(dpy, root);
	} 
	if ((oldShowPtr^showPtr)&4) {
		oldShowPtr ^= 4;
		oldShowPtr |= 2;
		return;
	}
	// oldShowPtr &= (1|2|4|8|16); // sanitize
	if (oldShowPtr == showPtr) return;
	if (--cnt) goto repeat;
	forceUngrab();
	DBG("setShowCursor() loop");
	if (XPending(dpy)) return;
	goto repeat0;
}

static void _set_bmap(_uint g, _short i, _uint j){
	switch (i) {
	    case 1:
		SET_BMAP(g|(3<<j),i,0);
		break;
	    case 2:
		break;
	    case 3:
		SET_BMAP(g|(1<<j),i,0);
	    default:
		SET_BMAP(g|(2<<j),i,0);
		SET_BMAP(g|(3<<j),i,0);
	}
}

static void initmap(){
	_uint i,j;
	for(i=1; i<8; i++){
		_uint g = 0;
		_uint j3 = 0;
		for (j=1; j<=pi[p_maxfingers]; j++) {
			j3 += 3;
			g = (g<<3)|i;
			if (i<4 ? (j==1) : (j>=pi[p_minfingers])) _set_bmap(g,i,j3);
			if (j==1) continue;
			if (i==BUTTON_DND) continue;
			if (i<4 ? (j>2) : ((j-1)<pi[p_minfingers])) continue;
//			_uint g1 = BUTTON_DND, g2 = 7, k;
//			for (k=0; k<j; k++) {
//				_set_bmap((g & ~g2)|g1,i,j3);
//				g1 <<= 3;
//				g2 <<= 3;
//			}
		}
	}
}

static void _scr_size(){
	minf_t m = minf0;
	minf0.width1 = minf0.width = DisplayWidth(dpy,screen);
	minf0.height1 = minf0.height = DisplayHeight(dpy,screen);
	minf0.mwidth1 = minf0.mwidth = DisplayWidthMM(dpy,screen);
	minf0.mheight1 = minf0.mheight = DisplayHeightMM(dpy,screen);

	if (!xrr && minf0.mwidth > minf0.mheight && minf0.width < minf0.height && minf0.mheight) {
		minf0.rotation = RR_Rotate_90;
		minf0.mwidth = minf0.mheight1;
		minf0.mheight = minf0.mwidth1;
	}
	if (minf0.rotation&(RR_Rotate_90|RR_Rotate_270)) {
		minf0.r90 = 1;
		minf0.mwidth1 = minf0.mheight;
		minf0.mheight1 = minf0.mwidth;
		minf0.width1 = minf0.height;
		minf0.height1 = minf0.width;
	}
	if (xrevent1 < 0 || memcmp(&minf0,&m,sizeof(m))) {
		DINF(1) dinf->type |= o_changed;
		oldShowPtr |= 8|16;
	}

	if (w_width == minf0.width && w_height == minf0.height && w_mwidth == minf0.mwidth && w_mheight == minf0.mheight) {
		_wait_mask &= ~_w_screen;
		dpm0w = w_dpmw;
		dpm0h = w_dpmh;
	} else {
		dpm0w = minf0.mwidth ? (.0+minf0.width)/minf0.mwidth : 0;
		dpm0h = minf0.mheight ? (.0+minf0.height)/minf0.mheight : 0;
		DPI_EQ(dpm0w,dpm0h);
		if ((_wait_mask & _w_screen) && w_width == minf0.width && w_height == minf0.height && !_DPI_CH(dpm0w, dpm0h, w_dpmw, w_dpmh, 0)) _wait_mask &= ~_w_screen;
	}
}
#endif

static void getPropWin1(){
	if (getWProp(root,aActWin,XA_WINDOW,sizeof(Window)))
		win1 = *(Window*)ret;
}

static void getWinGrp(){
	if (win1==None) {
		int revert;
#ifdef XTG
		if (kbdDev) XIGetFocus(dpy,kbdDev,&win1);
		if (win1==None)
#endif
		    XGetInputFocus(dpy, &win1, &revert);
		if (win1==None) win1 = root;
		if (win1 == win) return;
	}
	win = win1;
	grp1 = 0;
	if (win!=root && getWProp(win,aKbdGrp,XA_CARDINAL,sizeof(CARD32)))
		grp1 = *(CARD32*)ret;
	while (grp1 != grp && XkbLockGroup(dpy, XkbUseCoreKbd, grp1)!=Success && grp1) {
		XDeleteProperty(dpy,win,aKbdGrp);
		grp1 = 0;
	}
#ifdef XSS
	winWMState();
#endif
}

static void setWinGrp(){
	printGrp();
	if (win!=root)
		XChangeProperty(dpy,win,aKbdGrp,XA_CARDINAL,32,PropModeReplace,(unsigned char*) &grp1,1);
	grp = grp1;
}


int (*oldxerrh) (Display *, XErrorEvent *);
static int xerrh(Display *dpy, XErrorEvent *err){
	_error = err->error_code;
#ifdef XTG
	char *c = "";
	static int oldErr = 0;
	if (err->request_code == xropcode) {
#ifndef _X11_XLIBINT_H_
		c = "RANDR";
#endif
		switch (err->minor_code) {
		case X_RRQueryOutputProperty:
		case X_RRChangeOutputProperty:
		case X_RRDeleteOutputProperty: goto ex;
		case X_RRSetCrtcConfig:
		case X_RRSetScreenSize: goto msg;
		}
	} else if (err->request_code == xfopcode) {
#ifndef _X11_XLIBINT_H_
		c = "XFixes";
#endif
		switch (err->minor_code) {
		case X_XFixesShowCursor:
			// XShowCursor() before XHideCursor()
			curShow = 1;
			oldShowPtr |= 1;
			goto ex;
		}
	} else if (err->request_code == xiopcode) {
#ifndef _X11_XLIBINT_H_
		c = "XI";
#endif
		switch (err->minor_code) {
		case  X_XIDeleteProperty:
		case X_XIQueryPointer: goto ex; // FloatingSlave keyboard
		}
		switch (err->error_code) {
		case BadAccess: // second XI_Touch* root listener
			curShow = 1;
			oldShowPtr |= 1;
			goto ex;
		//return 0;
		}
		goto msg;
	}
#ifdef __USE_DPMS
	    else if (err->request_code == dpmsopcode) {
#ifndef _X11_XLIBINT_H_
		c = "DPMS";
#endif
	}
#endif
#endif
	switch (err->error_code) {
	    case BadDrawable:
	    case BadWindow:
		if (err->resourceid == win1) win1 = None;
		else if (err->resourceid == win) return 0;
		break;
#ifdef XTG
	    case BadAccess: // second XI_Touch* root listener/etc
		oldShowPtr |= 2|8|16;
		showPtr = 1;
		break;
#endif
	    default:
		oldxerrh(dpy,err);
	}
msg:
#ifdef XTG
	if (oldErr == err->error_code) return 0;
	oldErr=err->error_code;

	char s[256];
	s[sizeof(s)-1] = s[0] = 0;
	XGetErrorText(dpy,err->error_code,s,sizeof(s)-1);

#ifdef _X11_XLIBINT_H_
	_XExtension *ext;
	for (ext = dpy->ext_procs; ext; ext = ext->next) if (ext->codes.major_opcode == err->request_code) {
		c = ext->name;
		break;
	}
#endif

	ERR("X type=%i XID=0x%lx serial=%lu codes: error=%i request=%i minor=%i :: %s: %s",
		err->type, err->resourceid, err->serial, err->error_code,
		err->request_code, err->minor_code, c, s);
#endif
ex:
//	return err->error_code;
	return 0;
}

static void init(){
	int evmask = PropertyChangeMask;
	int ierr;

	screen = DefaultScreen(dpy);
	root = DefaultRootWindow(dpy);
//	XGetWindowAttributes(dpy, root, &wa);
	aActWin = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	aKbdGrp = XInternAtom(dpy, "_KBD_ACTIVE_GROUP", False);
	aXkbRules = XInternAtom(dpy, "_XKB_RULES_NAMES", False);
#ifdef XSS
#ifdef USE_XSS
	xss_enabled = XScreenSaverQueryExtension(dpy, &xssevent, &ierr);
#endif
#ifdef USE_DPMS
	dpms_enabled = DPMSQueryExtension(dpy, &dpmsevent, &ierr) && DPMSCapable(dpy);
#endif
#endif

#if _WIN_STATE
	aAbove = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
	aBelow = XInternAtom(dpy, "_NET_WM_STATE_BELOW", False);
#endif

#ifdef XSS
	aWMState = XInternAtom(dpy, "_NET_WM_STATE", False);
	aFullScreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	noXSS = False;
	// ClientMessage from XSendEvent()
	evmask |= SubstructureNotifyMask;
#endif
	XkbSelectEvents(dpy,XkbUseCoreKbd,XkbAllEventsMask,
		XkbStateNotifyMask
//		|XkbNewKeyboardNotifyMask
		);
	XkbSelectEventDetails(dpy,XkbUseCoreKbd,XkbStateNotify,
		XkbAllStateComponentsMask,XkbGroupStateMask);
#ifdef XTG
	getBPC();
	aFloat = XInternAtom(dpy, "FLOAT", False);
	aMatrix = XInternAtom(dpy, "Coordinate Transformation Matrix", False);
	aDevMon = XInternAtom(dpy, "xtg output", False);

	char s[64];
	strcpy(s,"xtg saved ");
	for (prI=0; prI<xrp_cnt; prI++) {
		a_xrp[prI] = XInternAtom(dpy, an_xrp[prI], False);
		strcpy(&s[10],an_xrp[prI]);
		a_xrp_save[prI] = XInternAtom(dpy, s, False);
	}
#if _BACKLIGHT == 2
	if (pi[p_safe]&4096) {
//		evmask |= ExposureMask;
		XISetMask(ximask0, XI_Leave);
		XISetMask(ximask0, XI_Enter);
#if 0
		XGCValues gcval;
		//Bool gexp = 1;
		//GC gc = XCreateGC(dpy, root, GCGraphicsExposures, &gcval);
		//XChangeGC(dpy,gc,GCGraphicsExposures,&gexp);
		//XSetGraphicsExposures(dpy, gc, True);
		XSetGraphicsExposures(dpy, XCreateGC(dpy, root, 0, &gcval), True);
#endif
	}
#endif
#if _BACKLIGHT == 1
	XISetMask(ximaskWin0, XI_Leave);
	XISetMask(ximaskWin0, XI_Enter);
//	XISetMask(ximaskWin0, XI_ButtonPress);
//	XISetMask(ximaskWin0, XI_ButtonRelease);
#endif
#ifdef USE_EVDEV
	aNode = XInternAtom(dpy, "Device Node", False);
	aABS = XInternAtom(dpy, "Device libevdev ABS", False);
#else
	aABS = XInternAtom(dpy, "xtg ABS", False);
#endif
#ifdef XSS
	if (pa[p_content_type]) val_xrp[xrp_ct].a = XInternAtom(dpy, pa[p_content_type], False);
	if (pa[p_colorspace]) val_xrp[xrp_cs].a = XInternAtom(dpy, pa[p_colorspace], False);
	val_xrp[xrp_rgb].a = XInternAtom(dpy, "Limited 16:235", False);
#endif
	val_xrp[xrp_sm].a = XInternAtom(dpy, "Full", False); // if native aspect

	XISetMask(ximaskButton, XI_ButtonPress);
	XISetMask(ximaskButton, XI_ButtonRelease);
	XISetMask(ximaskButton, XI_Motion);

	XISetMask(ximaskTouch, XI_TouchBegin);
	XISetMask(ximaskTouch, XI_TouchUpdate);
	XISetMask(ximaskTouch, XI_TouchEnd);
//	XISetMask(ximaskTouch, XI_TouchOwnership);

	XISetMask(ximaskRaw, XI_RawTouchBegin);
	XISetMask(ximaskRaw, XI_RawTouchUpdate);
	XISetMask(ximaskRaw, XI_RawTouchEnd);
	// XI_TouchOwnership no here

	// my pen with Z! pressure
	XISetMask(ximaskRaw, XI_RawButtonPress);
	XISetMask(ximaskRaw, XI_RawButtonRelease);
	XISetMask(ximaskRaw, XI_RawMotion);

#if 0
//	if (pi[p_floating]==3) {
		if (useRawTouch) {
			XISetMask(ximask0, XI_RawTouchBegin);
			XISetMask(ximask0, XI_RawTouchUpdate);
			XISetMask(ximask0, XI_RawTouchEnd);
//			XISetMask(ximask0, XI_TouchOwnership);
		} else {
			XISetMask(ximask0, XI_TouchBegin);
			XISetMask(ximask0, XI_TouchUpdate);
			XISetMask(ximask0, XI_TouchEnd);
		}
#if 0
		if (useRawButton) {
			XISetMask(ximask0, XI_RawButtonPress);
			XISetMask(ximask0, XI_RawButtonRelease);
			XISetMask(ximask0, XI_RawMotion);
		}
#endif
//	}
#endif

	XISetMask(ximaskButton, XI_PropertyEvent);
	XISetMask(ximaskTouch, XI_PropertyEvent);
	XISetMask(ximaskRaw, XI_PropertyEvent);
	XISetMask(ximask0, XI_PropertyEvent);

	XISetMask(ximask0, XI_HierarchyChanged);
	XISetMask(ximask0, XI_DeviceChanged);

	_grabX();
	XISelectEvents(dpy, root, ximask, Nximask);
	XIClearMask(ximask0, XI_HierarchyChanged);
	//XIClearMask(ximask0, XI_DeviceChanged);

	if (pf[p_res]<0 || mon || mon_sz) {
//		if (xrr = XRRQueryExtension(dpy, &xrevent, &ierr)) {
		// need xropcode
		if (xrr = XQueryExtension(dpy, "RANDR", &xropcode, &xrevent, &ierr)) {
			int m = RRScreenChangeNotifyMask;
#ifdef RROutputPropertyNotifyMask
			int v1 = 0, v2 = 0;
			XRRQueryVersion(dpy,&v1,&v2);
			v2 += (v1 << 10) - 1024;
			if (v2 > 1) {
				xrevent1 = xrevent + RRNotify;
				m |= RRCrtcChangeNotifyMask|RROutputChangeNotifyMask|RROutputPropertyNotifyMask;
#ifdef RRLeaseNotifyMask
				if (v2 > 5) m |= RRLeaseNotifyMask;
#endif
			}
#endif

			xrevent += RRScreenChangeNotify;
			XRRSelectInput(dpy, root, m);
			
			evmask |= StructureNotifyMask;
		} else evmask |= StructureNotifyMask; // ConfigureNotify
	}
	_wait_mask &= ~_w_screen;
	_scr_size();
	w_width = minf0.width;
	w_height = minf0.height;
	w_mwidth = minf0.mwidth;
	w_mheight = minf0.mheight;
	w_dpmw = dpm0w;
	w_dpmh = dpm0h;
	//forceUngrab();
#ifdef XSS
#ifdef USE_XSS
	if (xss_enabled) {
		XScreenSaverSelectInput(dpy, root, ScreenSaverNotifyMask);
		if (xss_state() == 1) timeSkip--;
	}
#endif
#ifdef USE_DPMS
	if (dpms_enabled) {
		if (xss_state() == 1) timeSkip--;
	}
#endif
#endif
	if (XQueryExtension(dpy, "XFIXES", &xfopcode, &xfevent, &ierr)) {
//		xfevent += XFixesCursorNotify;
//		XFixesSelectCursorInput(dpy,root,XFixesDisplayCursorNotifyMask);
	}
#endif
	XFlush(dpy); // reduce concurrence extensions/core
	XSelectInput(dpy, root, evmask);
	oldxerrh = XSetErrorHandler(xerrh);

	grp = NO_GRP;
	grp1 = 0;
	rul = NULL;
	rul2 =NULL;
	ret = NULL;
#ifdef XTG
	_signals(_eXit);
	curShow = 0;
	showPtr = 1;
	oldShowPtr |= 8|2;
	XFixesShowCursor(dpy,root);
#endif
	win = root;
#ifdef USE_THREAD
	if ((inotify = inotify_init()) >=0 )
		xthread_fork(&thread_inotify,NULL);
#endif
}

#ifdef XTG
static _short xiGetE(){
	if (!XGetEventData(dpy, &ev.xcookie)) return 0;
#undef e
#define e ((XIEvent*)ev.xcookie.data)
	TIME(T,e->time);
	return 1;
}
static void xiFreeE(){
	XFreeEventData(dpy, &ev.xcookie);
}

static _short chResDev(){
	resDev = devid;
	// force swap monitors if dynamic pan?
#if 1
	minf_t *m = minf2;
	findResDev();
	if (dinf2 && !(pi[p_safe]&64) && minf2 && minf2 != m) fixGeometry();
#else
	if (findResDev() == 2 && !(dinf2->type&o_absolute)) minf2 = NULL
#endif
	if (dinf2) {
		xabs2 = (void*)&dinf2->xABS;
		if (!minf2) minf2 = &minf0;
		return 1;
	}
	resDev = 0;
	return 0;
}
#endif

#ifdef XTG

#undef e
#define e ((XIRawEvent*)ev.xcookie.data)

#if defined(__BYTE_ORDER) &&  __BYTE_ORDER == __LITTLE_ENDIAN
#define _xabs_ok(p) ((xabs1=&xabs2[p])->en && e->valuators.mask_len > xabs1->np && (e->valuators.mask[xabs1->np]&(xabs1->nm)))
#else
#define _xabs_ok(p) ((xabs1=&xabs2[p])->en && e->valuators.mask_len > xabs1->np && XIMaskIsSet(e->valuators.mask, xabs1->number))
#endif

#define _val_z2() z2 = (z_en = _xabs_ok(2)) ? e->valuators.values[xabs1->number] - xabs1->min : 0;

double x2,y2;
double z2;
_short z_en;

static _short raw2xy(){
double xx;
int i;
	if (resDev != devid && !chResDev()) return 0;
	if (!_xabs_ok(0) || !_xabs_ok(1)) return 0;

	// can use raw_values or valuators.values
	// raw_values have 0 on TouchEnd
	// valuators - on touchsceen & libinput - mapped to Screen
	// IMHO there are just touchpad behaviour.
	// use simple valuators, but keep alter code here too
	_val_z2();
#ifdef FAST_VALUATORS
	if ((dinf2->fast&3) == 3) {
		x2 = e->valuators.values[xabs2[0].number]/dinf2->fastABS[0];
		y2 = e->valuators.values[xabs2[1].number]/dinf2->fastABS[1];
		return 1;
	}
#endif
	// bug: no values
	if (end) {
		end |= 2; // end&2 - no coordinates
		return 1;
	}
	x2 = _valuate(e->raw_values[xabs2[0].number],&xabs2[0],minf2->width1);
	y2 = _valuate(e->raw_values[xabs2[1].number],&xabs2[1],minf2->height1);
	switch (minf2->rotation) {
	    case RR_Rotate_0:
		x2 += minf2->x;
		y2 += minf2->y;
		break;
	    case RR_Rotate_90:
		xx = x2;
		x2 = minf2->x + minf2->height1 - y2;
		y2 = minf2->y + xx;
		break;
	    case RR_Rotate_180:
		x2 = minf2->x + minf2->width1 - x2;
		y2 = minf2->y + minf2->height1 - y2;
		break;
	    case RR_Rotate_270:
		xx = x2;
		x2 = minf2->x + y2;
		y2 = minf2->y + minf2->width1 - xx;
		break;
	    default:
		ERR("not implemented rotation type");
		return 0;
	}
	return 1;
}
#endif

int main(int argc, char **argv){
#ifdef XTG
	_short i;
	int opt;
	_short tt,g,bx,by;
	_uint k;
	_uint vl;
	int detail;
	double x1,y1,z1,xx,yy,zz,res;
	TouchTree *m;
	Touch *to;

	while((opt=getopt(argc, argv, pc))>=0){
		_short j;
		for (j=i=0; pc[j] && pc[j] != opt; j++) if (pc[j] != ':') i++;
		if (i>=MAX_PAR) goto help;
		if (i>=p_1) pi[i] = 1;
		else if (optarg) {
			pa[i] = optarg;
			pf[i] = atof(optarg);
			pi[i] = atoi(optarg);
		}
	}
	initmap();
#endif
	opendpy();
#ifdef XTG
	char **a;
	for (a=argv+optind; *a; a++) {
		int x, y;
		unsigned int z = 0;
		char k[128];
		switch (sscanf(*a,"%i:%i:%127s",&x,&y,(char*)&k)) {
		    case 3:
			z = XKeysymToKeycode(dpy,XStringToKeysym((char*)&k));
		    case 2:
			SET_BMAP(x,y,z);
			continue;
		}
		ERR("invalid map item '%s', -h to help",*a);
		return 1;
	}
	if (pi[p_1]) {
		_wait_mask &= ~_w_none;
		pi[p_safe] &= ~(32+64);
	}
	if (pi[p_y]) {
		pf[p_dpi] = pi[p_dpi] = 96;
		pi[p_safe] = 1+16+32;
		pi[p_Safe] = 1+4;
	}
	if (pi[p_Y]) {
		pi[p_safe] = 1+4+128+4096;
		pi[p_Safe] = 1+8;
		pa[p_content_type] = "Cinema";
		pa[p_colorspace] = "DCI-P3_RGB_Theater";
	}
	resX = resY = pf[p_res] < 0 ? 1 : pf[p_res];
	strict = pa[p_mon] &&  pa[p_mon][0] == '-' && !pa[p_mon][1];
	mon_sz = pi[p_mon] < 0;
	mon_grow = -pf[p_mon];
	if (!strict && !mon_sz && pa[p_mon] && *pa[p_mon])
		mon = XInternAtom(dpy, pa[p_mon], False);
	resXY = !mon && pf[p_res]<0;
	if (pi[p_floating]&4) {
		useRawTouch = 1;
		pi[p_floating] ^= 4;
	}
	if (pi[p_floating]&8) {
		useRawButton = 1;
		pi[p_floating] ^= 8;
	}
	if (pi[p_help]) {
help:
		_short j;
		pf[p_safe] = pi[p_safe];
		pf[p_Safe] = pi[p_Safe];
		printf("Usage: %s {<option>} {<value>:<button>[:<key>]}\n",argv[0]);
		for(i=j=0; i<MAX_PAR; i++) {
			if (pc[j] == ':') j++;
			printf("	-%c ",pc[j++]);
			if (i<p_1) {
				if (pf[i] != pi[i]) printf("%.1f",pf[i]);
				else if (!pf[i] && pa[i]) printf("'%s'",pa[i]);
				else printf("%i",pi[i]);
			}
			printf(" %s\n",ph[i]);
		}
		printf("\n<value> is octal combination, starting from x>0, where x is last event type:\n"
			"	1=begin, 2=update, 3=end\n"
			"	bit 3(+4)=border\n"
			"	(standard scroll buttons: up/down/left/right = 5/4/7/6)\n"
			"<button> 0..%i (X use 1-9)\n"
			"	button 0xf is special: this is key"
			"\ndefault map:",MAX_BUTTON);
		initmap();
		_print_bmap(0, 0, &bmap);
		printf("\n\nRoute 'hold' button 3 to 'd-n-d' button 2: 013:2\n"
			"Use default on-TouchEnd bit - oneshot buttons:\n"
			"  2-fingers swipe right to 8 button: 0277:0 0377:0x88\n"
			"  Audio prev/next keys to 2-left/right: 0277:0x8f:XF86AudioNext 0377:0x8f:XF86AudioNext 0266:0x8f:XF86AudioPrev 0366:0x8f:XF86AudioPrev\n"
			"   - same for 1-left/right from screeen border: 057:0x8f:XF86AudioNext 056:0x8f:XF86AudioPrev\n"
			"\nMonitor per touch device (if not '-R <name>'):\n"
			"  xinput set-prop <device> --type=atom 'xtg output' <monitor>\n"
			);
		return 0;
	}
//	if (pi[p_dpi]) 
		pf[p_dpi] /= INCH;
#endif
	if (!dpy) return 1;
	init();
	printGrp();
	getPropWin1();
	while (1) {
		do {
			if (win1 != win) getWinGrp();
			else if (grp1 != grp) setWinGrp();
		} while (win1 != win); // error handled
ev:
#ifdef XTG
		if (timeHold) {
			Time t = T - timeHold;
			if (t >= pi[p_hold] || _delay(pi[p_hold] - t)) {
				forceUngrab();
				to->g = BUTTON_HOLD;
				timeHold = 0;
				goto hold;
			}
		}
ev2:
		if (showPtr != oldShowPtr) setShowCursor();
		//if (!_wait_mask) break;
		forceUngrab();
		if (!_wait_mask) break;
#endif
		XNextEvent(dpy, &ev);
		//DBG("ev %i",ev.type);
		switch (ev.type) {
#ifdef XTG
		    case GenericEvent:
			if (ev.xcookie.extension == xiopcode) {
				//DBG("ev xi %i %lu",ev.xcookie.evtype,ev.xcookie.serial);
#undef e
#define e ((XIDeviceEvent*)ev.xcookie.data)
				switch (ev.xcookie.evtype) {
				    case XI_TouchBegin:
				    case XI_TouchUpdate:
				    case XI_TouchEnd:
					showPtr = 1;
//					if (!tdevs2 || !xiGetE()) goto ev;
					if (!xiGetE()) goto ev;
					devid = e->deviceid;
					detail = e->detail;
					if (resDev != devid && !chResDev()) goto evfree;
					end = (ev.xcookie.evtype == XI_TouchEnd)
					    | ((!!(e->flags & XITouchPendingEnd))<<2); // end&4 - drop event
					x2 = e->root_x;
					y2 = e->root_y;
					_val_z2();
					break;
#undef e
#define e ((XIRawEvent*)ev.xcookie.data)
				    case XI_RawButtonPress:
					showPtr = 1;
					if (!xiGetE()) goto ev;
					if (resDev != devid && !chResDev()) goto evfree;
					if (dinf2->zstate == 5 && !dinf2->zdetail && dinf2->zserial == e->serial && dinf2->ztime == T) {
						// source valuator is controlled in XI_RawMotion
						dinf2->zstate = 1;
						dinf2->zdetail = e->detail;
					} else {
						XTestFakeButtonEvent(dpy,e->detail,1,0);
					}
					xiFreeE();
					goto ev2;
				    case XI_RawButtonRelease:
					showPtr = 1;
					if (!xiGetE()) goto ev;
					if (resDev != devid && !chResDev()) goto evfree;
					if (dinf2->zdetail == e->detail && dinf2->zstate) {
						switch (dinf2->zstate) {
						    case 1:
							if (dinf2->zserial == e->serial && dinf2->ztime == e->time) break;
							XTestFakeButtonEvent(dpy,e->detail,1,0);
						    case 2:
						    case 3:
							XTestFakeButtonEvent(dpy,e->detail,0,0);
						}
						dinf2->zdetail = 0;
						dinf2->zstate = 0;
					} else XTestFakeButtonEvent(dpy,e->detail,0,0);
					xiFreeE();
					goto ev2;
				    case XI_RawMotion:
					showPtr = 1;
					if (!xiGetE()) goto ev;
					devid = e->deviceid;
					if (!raw2xy()) goto evfree1;
					XTestFakeMotionEvent(dpy,screen,x2,y2,0);
					if (!z_en) goto evfree;
					switch (dinf2->zstate) {
					    case 0:
						if (dinf2->z == 0. && z2 > 0.) dinf2->zstate = 5;
						break;
					    case 1:
						if (dinf2->z - z2 > dinf2->mindiffZ) dinf2->zstate = 2;
						else if (z2 < dinf2->z) goto z_noise;
						else if (dinf2->maxZ != 0. && z2 >= dinf2->maxZ) dinf2->zstate = 3;
						else break;
						//DBG("pr %f",z2 - dinf2->z);
						XTestFakeButtonEvent(dpy,dinf2->zdetail,1,0);
						break;
					    case 2:
						if (z2 - dinf2->z > dinf2->mindiffZ) dinf2->zstate = 1;
						else if (z2 > dinf2->z) goto z_noise;
						else if (z2 <= dinf2->minZ) dinf2->zstate = 4;
						else break;
						XTestFakeButtonEvent(dpy,dinf2->zdetail,0,0);
						break;
					    case 3:
						if (dinf2->maxZ != 0. && z2 >= dinf2->maxZ) break;
						dinf2->zstate = 1;
						XTestFakeButtonEvent(dpy,dinf2->zdetail,0,0);
						break;
					    case 4:
						if (z2 <= dinf2->minZ) break;
						dinf2->zstate = 2;
						XTestFakeButtonEvent(dpy,dinf2->zdetail,1,0);
						break;
					    case 5: // check button skipped or dup
						if (dinf2->zserial == e->serial && dinf2->ztime == e->time && z2 == dinf2->z) break;
						dinf2->zstate = 0;
						break;
					}
					switch (dinf2->z_ewmh) {
					    case 0:
					    case 1:
						dinf2->z = z2;
						break;
					    case 2:
						dinf2->z = (dinf2->z + z2) / 2;
						break;
					    default:
						dinf2->z = (dinf2->z*(dinf2->z_ewmh-1)+z2)/dinf2->z_ewmh;
						break;
					}
					dinf2->z = z2;
z_noise:
					dinf2->zserial = e->serial;
					dinf2->ztime = T;
					xiFreeE();
					goto ev2;
				    case XI_RawTouchBegin:
				    case XI_RawTouchUpdate:
				    case XI_RawTouchEnd:
					showPtr = 1;
//					if (!tdevs2 || !xiGetE()) goto ev;
					if (!xiGetE()) goto ev;
					detail = e->detail;
					devid = e->deviceid;
					end = (ev.xcookie.evtype == XI_RawTouchEnd)
					    | ((!!(e->flags & XITouchPendingEnd))<<2); // end&4 - drop event
					if (!raw2xy()) goto evfree1;
					break;
#undef e
#define e ((XIDeviceChangedEvent*)ev.xcookie.data)
				    case XI_DeviceChanged:
#ifndef MINIMAL
					if (xiGetE()) {
						devid = 0;
						//showPtr = 1;
						if (e->reason != XISlaveSwitch) {
							oldShowPtr |= 2;
							if (e->reason != XIDeviceChange) goto evfree1;
						}
						dinf1 = NULL;
						if (e->sourceid && e->sourceid != e->deviceid) {
							DINF(dinf->devid == e->sourceid) {
								showPtr = !TDIRECT(dinf->type);
								if (oldShowPtr&2) goto evfree1;
								dinf1 = dinf;
								break;
							}
						} //else if (e->reason == XIDeviceChange) devid = e->deviceid;
						_short type1 = xiClasses(e->classes,e->num_classes);
						if (TDIRECT(type1)) showPtr = 0;
						//else showPtr = 1; // vs. XTEST
						//if (devid) { oldShowPtr ^= 2; xiDevice(type1)}; goto evfree1;}
						if (oldShowPtr&2) goto evfree1;
						devid = e->deviceid;
						DINF(dinf->devid == e->deviceid) {
							if (!(dinf->type&o_master) && (DEV_CMP_MASK&(dinf->type,type1)))
								oldShowPtr |= 2;
							else memcpy(&dinf->xABS,&xABS,sizeof(xABS));
							dinf1 = dinf;
							break;
						}
						xiFreeE();
					} else
#endif
					    oldShowPtr |= 2;
					continue;
#undef e
#define e ((XITouchOwnershipEvent*)ev.xcookie.data)
#if 0
				    case XI_TouchOwnership:
					if (!xiGetE()) goto ev;
					devid = e->deviceid;
					detail = e->touchid;
					// with event set: server not allocate touch history && set chils
					DBG("XI_TouchOwnership flags 0x%x child 0x%lx",e->flags,e->child);
					if (resDev != devid && !chResDev()) goto evfree;
					// I dont want more, so drop whole touch
					// touch flags: XITouchPendingEnd, XITouchEmulatingPointer
//					XIAllowTouchEvents(dpy,devid,detail,e->child,XIRejectTouch);
//					XIAllowTouchEvents(dpy,devid,detail,e->child,XIAcceptTouch);
//					XIAllowTouchEvents(dpy,devid,detail,e->child,XIReplayDevice);
					XFlush(dpy);
					xiFreeE();
					// drop if exists
					for(i=P; i!=N; i=TOUCH_N(i)){
						Touch *t1 = &touch[i];
						if (t1->touchid != detail || t1->deviceid != devid) continue;
						to = t1;
						goto drop_touch;
					}

					break;
#endif
#undef e
#define e ((XIPropertyEvent*)ev.xcookie.data)
				    case XI_PropertyEvent:
					if (xiGetE()) {
						Atom p = e->property;
						dinf1 = NULL;
						int devid1 = e->deviceid;
						DINF(dinf->devid == devid1) {
							dinf1 = dinf;
							break;
						}
						unsigned long serial = e->serial;
						xiFreeE();
						if (p == aMatrix) {
#ifdef TRACK_MATRIX
							float m[9];
							devid = devid1;
							DBG("xi device %i prop '%s' changed %s (%lu/%lu)",devid,natom(0,p),
								!dinf1?"unlikely to me - untracked":
								!xiGetProp(aMatrix,aFloat,&m,9,0)?
								"unknown/bad":memcmp(m,dinf1->matrix,sizeof(m))?
								    "not our value!":"our",
								T,serial
								);
#else
							DBG("xi device %i prop '%s' changed",devid1,natom(0,p));
							continue;
#endif
						}
						if (p==aDevMon) {
							if (dinf1) dinf1->mon = 0;
							oldShowPtr |= 16;
							continue;
						}
						//if (p==aABS) goto ev2;
						continue;
					}
					oldShowPtr |= 2;
					continue;
#undef e
#define e ((XIHierarchyEvent*)ev.xcookie.data)
				    case XI_HierarchyChanged:
					oldShowPtr |= 2;
					continue;
#if _BACKLIGHT
#undef e
#define e ((XIEnterEvent*)ev.xcookie.data)
				    case XI_FocusIn:
					win1 = None;
					continue;
				    case XI_Leave:
				    case XI_Enter:
					if (xiGetE()) {
						//if (e->detail != XINotifyAncestor)
						    switch (e->mode) {
#if 0
						    case XINotifyGrab:
						    case XINotifyUngrab:
							break;
						    default:
#else
						    case XINotifyUngrab:
						    case XINotifyNormal:
						    case XINotifyWhileGrabbed:
#endif
#if _BACKLIGHT == 1
							chBL(e->event,2,ev.xcookie.evtype==XI_Enter);
#else
							chBL(None,e->root_x,e->root_y,1,1,ev.xcookie.evtype!=XI_Enter);
#endif
							break;
						}
						xiFreeE();
					}
					continue;
#endif
#undef e
#define e ((XIDeviceEvent*)ev.xcookie.data)
				    //case XI_ButtonPress:
				    //case XI_ButtonRelease:
					//showPtr = 1;
					//timeHold = 0;
					//goto ev2;
				    default:
					showPtr = 1;
					timeHold = 0;
					goto ev2;
				}
touch_common:
				xiFreeE();
//				showPtr = 0;
				showPtr = !TDIRECT(dinf2->type);
				x2 += pf[p_round];
				y2 += pf[p_round];
				res = resX;
				tt = 0;
				m = &bmap;
				g = ((_int)x2 <= minf2->x) ? BUTTON_RIGHT : ((_int)x2 >= minf2->x2) ? BUTTON_LEFT : ((_int)y2 <= minf2->y) ? BUTTON_UP : ((_int)y2 >= minf2->y2) ? BUTTON_DOWN : 0;
				if (g) tt |= PH_BORDER;
				for(i=P; i!=N; i=TOUCH_N(i)){
					Touch *t1 = &touch[i];
					if (t1->touchid != detail || t1->deviceid != devid) continue;
					to = t1;
					goto tfound;
				}
				if (oldShowPtr && N!=P  // vs. event loss on event switches. so, skip to static events
				    && pi[p_floating] != 3
//				    && pi[p_floating] != 0
				    ) {
					ERR("Drop %i touches by input %i detail %i",TOUCH_CNT,devid,detail);
					N=P;
				}
//				{
					// new touch
					timeHold = 0;
					if (end) goto ev2;
					to = &touch[N];
					N=TOUCH_N(N);
					if (N==P) P=TOUCH_N(P);
					to->touchid = detail;
					to->deviceid = devid;
					TIME(to->time,T);
					to->z = z1 = z2;
					to->x = x1 = x2;
					to->y = y1 = y2;
					to->tail = 0;
					to->g = g;
					to->g1 = g;
					_short nt;
					to->n = nt = 1;
					if (T <= timeSkip) goto invalidateT;
					_short n = TOUCH_P(N);
					_short nt1 = 1;
					for (i=P; i!=n; i=TOUCH_N(i)) {
						Touch *t1 = &touch[i];
						if (t1->deviceid != to->deviceid) continue;
						switch (t1->g) {
						    case BUTTON_DND: continue;
						    case BAD_BUTTON: goto invalidate1;
						}
						if (nt1++ == 1) nt = ++t1->n;
						else if (++t1->n != nt) goto invalidate;
						if (m) m = m->gg[t1->g&7];
					}
					if (nt1 != nt) goto invalidate;
					to->n = nt;
					if (!m) goto ev2;
					if (!g && nt == 1) {
						timeHold = T;
						goto ev;
					}
hold:
					if ((m = m->gg[to->g&7]) && (m = m->gg[tt|=1]) && (g = m->g))
						goto found;
					goto ev2;
invalidate:
					for (i=P; i!=n; i=TOUCH_N(i)) {
						Touch *t1 = &touch[i];
						if (t1->deviceid != to->deviceid) continue;
						if (t1->g == BUTTON_DND) continue;
						t1->g = BAD_BUTTON;
					}
invalidate1:
					to->g = BAD_BUTTON;
					goto ev2;
invalidateT:
					timeSkip = T; // allow bad time value, after XSS only once
					to->g = BAD_BUTTON;
					// oldShowPtr can be changed only on single touch Begin
					// so, if checked at least 1 more touch - skip this
					if (oldShowPtr) continue;
					goto ev2;
//				}
tfound:
				if (end&4) goto drop_touch;
				z1 = to->z;
				x1 = to->x;
				y1 = to->y;
				switch (to->g) {
				    case BAD_BUTTON: goto skip;
				    case BUTTON_DND: goto next_dnd;
				}
				if (end&2) {
					z2 = z1;
					x2 = x1;
					y2 = y1;
				}
				xx = x2 - x1;
				yy = y2 - y1;
				bx = 0;
				by = 0;
				if (xx<0) {xx = -xx; bx = BUTTON_LEFT;}
				else if (xx>0) bx = BUTTON_RIGHT;
				if (yy<0) {yy = -yy; by = BUTTON_UP;}
				else if (yy>0) by = BUTTON_DOWN;
				if (xx<yy) {
					double s = xx;
					xx = yy;
					yy = s;
					bx = by;
					res = resY;
				}

				if (bx) {
				    if (bx == to->g1) xx += to->tail;
				    if (xx < res || xx/(yy?:1) < pf[p_xy]) bx = 0;
				}

				if (!bx && !to->g1 && to->n==1) {
					if (!end) goto ev;
					bx = 1;
				}

				if (bx) to->g1 = bx;
				to->g = bx;
				timeHold = 0;

				for (i=P; i!=N; i=TOUCH_N(i)) {
					Touch *t1 = &touch[i];
					if (t1->deviceid != to->deviceid) continue;
					if (t1->g == BUTTON_DND) continue;
					m = m->gg[t1->g & 7];
					if (!m) goto gest0;
				}
				tt |= end + 2;
				m = m->gg[tt];
				if (!m) goto gest0;
				g = m->g;
found:
				if (g & END_BIT) {
					g ^= END_BIT;
					xx = 0;
					for(i=P; i!=N; i=TOUCH_N(i)) {
						Touch *t1 = &touch[i];
						if (t1->deviceid != to->deviceid) continue;
						if (t1->g == BUTTON_DND) continue;
						t1->g = BAD_BUTTON;
					}
				}
				switch (g) {
				    case 0:
gest0:
//					if (end && TOUCH_CNT == 1)
//						XTestFakeMotionEvent(dpy,screen,x2,y2,0);
					goto skip;
				    case BUTTON_DND:
					XTestFakeMotionEvent(dpy,screen,x1,y1,0);
					XTestFakeButtonEvent(dpy,BUTTON_DND,1,0);
next_dnd:
					if (!(end&2)) XTestFakeMotionEvent(dpy,screen,x2,y2,0);
					//if (end) XTestFakeButtonEvent(dpy,BUTTON_DND,0,0);
					break;
				    case BUTTON_KEY:
					k = m->k;
					XTestFakeMotionEvent(dpy,screen,x1,y1,0);
					XTestFakeKeyEvent(dpy,k,1,0);
					for (xx-=res;xx>=res;xx-=res) {
						XTestFakeKeyEvent(dpy,k,0,0);
						XTestFakeKeyEvent(dpy,k,1,0);
					}
					XTestFakeMotionEvent(dpy,screen,x2,y2,0);
					XTestFakeKeyEvent(dpy,k,0,0);
					break;
				    case BUTTON_HOLD:
					to->g = BAD_BUTTON;
				    case 1:
					xx = 0;
				    default:
					XTestFakeMotionEvent(dpy,screen,x1,y1,0);
					XTestFakeButtonEvent(dpy,g,1,0);
					for (xx-=res;xx>=res;xx-=res) {
						XTestFakeButtonEvent(dpy,g,0,0);
						XTestFakeButtonEvent(dpy,g,1,0);
					}
					XTestFakeMotionEvent(dpy,screen,x2,y2,0);
					XTestFakeButtonEvent(dpy,g,0,0);
				}
//				XFlush(dpy);
				to->tail = xx;
				TIME(to->time,T);
				to->z = z2;
				to->x = x2;
				to->y = y2;
skip:
				if (!end) goto ev2;
drop_touch:
				if (to->g == BUTTON_DND) XTestFakeButtonEvent(dpy,BUTTON_DND,0,0);
#ifdef TOUCH_ORDER
				_short t = (to - &touch[0]);
				if (t == P) TOUCH_N(P);
				else {
					for(i=TOUCH_N(t); i!=N; i=TOUCH_N(t = i)) touch[t] = touch[i];
					N=TOUCH_P(N);
				}
#else
				N=TOUCH_P(N);
				Touch *t1 = &touch[N];
				if (to != t1) *to = *t1;
#endif
				goto ev2;
			}
			goto ev;
evfree:
			xiFreeE();
			goto ev;
evfree1:
			xiFreeE();
			continue;
#endif
#ifdef XSS
#undef e
#define e (ev.xclient)
		    case ClientMessage:
			if (e.message_type == aWMState){
				if (e.window==win)
					WMState(e.data.l[1],e.data.l[0]);
			}
			goto ev;
#endif
#undef e
#define e (ev.xproperty)
		    case PropertyNotify:
			TIME(T,e.time);
			//fprintf(stderr,"Prop %s\n",natom(0,e.atom));
			//if (e.window!=root) break;
			if (e.atom==aActWin) {
				win1 = None;
				if (e.state==PropertyNewValue) getPropWin1();
			} else if (e.atom==aXkbRules) {
#if 0
				XkbStateRec s;
				XkbGetState(dpy,XkbUseCoreKbd,&s);
				grp1 = s.group;
#endif
				grp = grp1 + 1;
				rul2 = NULL;
			}
			break;
#if _BACKLIGHT == 2
#undef e
#define e (ev.xcreatewindow)
		    case CreateNotify:
//			setMapped(e.window,1);
//			break;
#undef e
#define e (ev.xmap)
		    case MapNotify:
			setMapped(e.window,1);
//			DBG("map %x",e.window);
			break;

#undef e
#define e (ev.xreparent)
		    case ReparentNotify:
//			DBG("reparent %i %x",e.parent == root,e.window);
			setMapped(e.window,e.parent == root);
			break;

#undef e
#define e (ev.xdestroywindow)
		    case DestroyNotify:
//			setMapped(e.window,0);
//			break;
#undef e
#define e (ev.xunmap)
		    case UnmapNotify:
			setMapped(e.window,0);
//			DBG("unmap %x",e.window);
			break;
#if 0
		    case NoExpose:
		    case GraphicsExpose:
		    case Expose:
			if (ev.any.window != root) break;
			// root redraw: empty area: rescan
			oldShowPtr |= 32;
			break;
#endif // 0
#endif // _BACKLIGHT == 2
/*
#undef e
#define e (ev.xgraphicsexpose)
		    case GraphicsExpose:
			break;
#undef e
#define e (ev.xnoexpose)
		    case NoExpose:
			DBG("noexpose %i",e.drawable == root);
			break;
		    case UnmapNotify:
		    case MapNotify:
		    case ReparentNotify:
			goto ev;
			break;
*/
#ifdef XTG
#if _BACKLIGHT == 1
#undef e
#define e (ev.xvisibility)
		    case VisibilityNotify:
			chBL(e.window,e.state != VisibilityUnobscured,2);
			goto ev;
#endif // _BACKLIGHT == 1
#if _BACKLIGHT
#if 0
#undef e
#define e (ev.xcrossing)
		    case EnterNotify:
		    case LeaveNotify:
			//TIME(T,e.time);
#if _BACKLIGHT == 1
			chBL(e.window,2,ev.type == EnterNotify);
#else
			chBL(None,e.x_root,e.y_root,1,1,ev.type != EnterNotify);
#endif
			goto ev;
#undef e
#define e (ev.xbutton)
		    case MotionNotify: // XButtonEvent = XMotionEvent
		    case ButtonPress:
		    case ButtonRelease:
			TIME(T,e.time);
			// trick from tint2
			XUngrabPointer(dpy, e.time);
			e.x = e.x_root;
			e.y = e.y_root;
			e.window = root;
			XSendEvent(dpy, root, False, ButtonPressMask, &ev);
#endif //0
#endif //_BACKLIGHT
#undef e
#define e (ev.xconfigure)
		    case ConfigureNotify:
			if (e.window == root) {
				//if (xrr) break;
				oldShowPtr |= 8|16;
#ifndef MINIMAL
#if 1
				if (XRRUpdateConfiguration(&ev)) _scr_size();
#else
				//minf0.width = e.width;
				//minf0.height = e.height;
				dpy->screens[screen].width = e.width;
				dpy->screens[screen].height  = e.height;
				_scr_size();
#endif
#endif
				break;
			}
#if _BACKLIGHT == 1
			if (e.override_redirect) {
			    if (fixWin(e.window,RECT(e))) break;
			}
#endif // _BACKLIGHT == 1
#if _BACKLIGHT == 2
			setMapped(e.window,0);
#endif // _BACKLIGHT == 2
#ifdef XSS
			if (!(pi[p_safe]&8192)) {
#ifndef MINIMAL
				// MINIMAL will check window property every move/resize step and twice on win init
				// but e.window != win (OB3), reduce approximated
				_short x = 0;
				MINF(minf->height == e.height && minf->y == e.y && minf->width == e.width && minf->x == e.x && (minf->type&o_active)) {
					x = 1;
					break;
				}
				if (noXSS != x)
#endif
				    winWMState();
			}
#endif
			break;
#endif
		    default:
#undef e
#define e ((XkbEvent)ev)
			if (ev.type == xkbEventType) {
				TIME(T,e.any.time);
				switch (e.any.xkb_type) {
#undef e
#define e (((XkbEvent)ev).state)
				    case XkbStateNotify:
					grp1 = e.group;
					break;
				    //case XkbNewKeyboardNotify:
					//break;
				}
				break;
			}
#ifdef XTG
#ifdef XSS
#ifdef USE_XSS
#undef e
#define e ((XScreenSaverNotifyEvent*)&ev)
			if (ev.type == xssevent) {
				TIME(T,e->time); // no sense for touch
				xssStateSet(e->state);
				switch (xssState) {
//				    case 3:
				    case 0:
					timeSkip = e->time;
					break;
				}
				break;
			}
#endif
#endif
			if (xrEvent()) break;
#undef e
#define e ((XFixesCursorNotifyEvent*)&ev)
//			if (ev.type == xfevent) {
//				TIME(T,e->timestamp);
//				break;
//			}
#endif
//			fprintf(stderr,"ev? %i\n",ev.type);
			break;
		}
	}
#ifdef XTG
exit:
	XFixesShowCursor(dpy,root);
	XFlush(dpy);
	XCloseDisplay(dpy);
#endif
}
