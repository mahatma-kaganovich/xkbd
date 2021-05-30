/*
	xtg v1.38 - per-window keyboard layout switcher [+ XSS suspend].
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
// todo
#define _BACKLIGHT
//#undef _BACKLIGHT
#define PROP_FMT
#undef PROP_FMT
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

typedef uint_fast8_t _short;
typedef uint_fast32_t _int;




Display *dpy = 0;
Window win = None, win1 = None;
int screen;
Window root;
Atom aActWin, aKbdGrp, aXkbRules;
unsigned char _error;

#undef _WIN_STATE

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
static inline long _min(long x,long y){ return x<y?x:y; }
static inline long _max(long x,long y){ return x>y?x:y; }

#ifdef XTG
//#define DEV_CHANGED
int devid = 0;
int xiopcode=0, xfopcode=0, xfevent=-100;
Atom aFloat,aMatrix,aABS,aDevMon;
XRRScreenResources *xrrr = NULL;
_short showPtr, oldShowPtr = 0, curShow;
_short _quit = 0;
_short __init = 1;
int floatFmt = sizeof(float) << 3;
int atomFmt = sizeof(Atom) << 3;
int winFmt = sizeof(Window) << 3;
_short useRaw = 0;
#define prop_int int32_t
int intFmt = sizeof(prop_int) << 3;
#ifdef USE_EVDEV
Atom aNode;
#endif
#ifdef XSS
XWindowAttributes wa;
#endif

#define MASK_LEN XIMaskLen(XI_LASTEVENT)
typedef unsigned char xiMask[MASK_LEN];
xiMask ximaskButton = {}, ximaskTouch = {}, ximask0 = {}, ximaskTouchRaw = {};
xiMask ximaskRoot = {};
_short Nximask = 1;
XIEventMask ximask[] = {{ .deviceid = XIAllDevices, .mask_len = MASK_LEN, .mask = (void*)&ximask0 },
//			 { .deviceid = XIAllMasterDevices, .mask_len = MASK_LEN, .mask = (void*)&ximaskRoot }
			};
//#define MASK(m) ((void*)ximask[0].mask=m)
#ifdef _BACKLIGHT
xiMask ximaskWin0 = {};
XIEventMask ximaskWin[] = {{ .deviceid = XIAllMasterDevices, .mask_len = MASK_LEN, .mask = (void*)&ximaskWin0 },
//			 { .deviceid = XIAllDevices, .mask_len = MASK_LEN, .mask = (void*)&ximaskWin0 }
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
	_int k;
	_short g;
} TouchTree;
TouchTree bmap = {};
static void SET_BMAP(_int i, _short x, _int key){
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
static void _print_bmap(_int x, _short n, TouchTree *m) {
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
#define p_safe 14
#define p_content_type 15
#define p_colorspace 16
#define MAX_PAR 17
char *pa[MAX_PAR] = {};
// android: 125ms tap, 500ms long
#define PAR_DEFS { 0, 1, 2, 1000, 2, -2, 0, 1, DEF_RR, 0,  14, 32, 72, 0, DEF_SAFE, 0, 0 }
int pi[] = PAR_DEFS;
double pf[] = PAR_DEFS;
char pc[] = "d:m:M:t:x:r:e:f:R:a:o:O:p:P:s:c:C:h";
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
	"		+4=use XI_RawTouch* events if possible\n", //f

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
	"max ...",
	"safe mode bits\n"
	"		1 don't change hierarchy of device busy/pressed (for -f 2)\n"
#ifdef USE_EVDEV
	"		2 XUngrabServer() for libevdev call\n"
#endif
	"		3(+4) XGrabServer() cumulative\n"
	"		4(+8) cursor recheck (tricky)\n"
	"		5(+16) no mon dpi\n"
	"		6(+32) no mon primary\n"
	"		7(+64) no vertical panning\n"
	"		8(+128) auto bits 6,7 on primary present - enable primary & disable panning\n"
	"		9(+256) use mode sizes for panning (errors in xrandr tool!)\n"
	"		10(+512) keep dpi different x & y (image panned non-aspected)\n"
	"		11(+1024) don't use cached input ABS\n"
	"		12(+2048) try delete or not create unused (saved) property\n"
#ifdef _BACKLIGHT
	"		13(+4096) track backlight-controlled monitors and off when empty (under construction)\n"
#endif	
#ifdef XSS
	"		14(+8192) skip fullscreen windows without ClientMessage (XTerm)\n"
#endif
	"		(auto-panning for openbox+tint2: \"xrandr --noprimary\" on early init && \"-s 135\" (7+128))\n",
#ifdef XSS
	"fullscreen \"content type\" prop.: see \"xrandr --props\" (I want \"Cinema\")",
#endif
#ifdef XSS
	"fullscreen \"Colorspace\" prop.: see \"xrandr --props\"  (I want \"DCI-P3_RGB_Theater\")\n",
#endif
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


#ifdef XSS
// DPMSInfo() and XScreenSaverQueryInfo() result is boolean (but type is "Status")
// use transparent values instead
#ifdef USE_XSS
_short xssState = 3;
#if ScreenSaverDisabled == 3 && ScreenSaverOn == 1 && ScreenSaverOff == 0
#define xssStateSet(s) xssState = (s)
#else
static void xssStateSet(_short s){
	switch (s) {
	    case ScreenSaverOff: xssState = 0; break;
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
#ifdef _BACKLIGHT
	xrp_bl,
#endif
#ifdef XSS
	xrp_ct,
	xrp_cs,
#endif
	xrp_cnt,
} xrp_t;

char *an_xrp[xrp_cnt] = {
	RR_PROPERTY_NON_DESKTOP,
#ifdef _BACKLIGHT
	RR_PROPERTY_BACKLIGHT,
#endif
#ifdef XSS
	"content type",
	"Colorspace",
#endif
};

Atom type_xrp[xrp_cnt] = {
	XA_INTEGER,
#ifdef _BACKLIGHT
	XA_INTEGER,
#endif
	XA_ATOM,
	XA_ATOM,
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
static void WMState(Atom *states, short nn){
	short i;
	noXSS1 = False;
	for(i=0; i<nn; i++) {
		if (states[i] == aFullScreen) {
			noXSS1 = True;
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
	getWProp(win,aWMState,XA_ATOM,sizeof(Atom));
	WMState(((Atom*)ret),n);
}
#endif

#ifdef XTG

_short grabcnt = 0;
//_int grabserial = -1;
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
	if ((pi[p_safe]&4) && !--grabcnt) {
		XFlush(dpy);
		_Xungrab();
	}
}

#define NdevABS 3

//float devABS[NdevABS] = {0,0,};

typedef struct _abs {
	double min,max;
	int resolution;
} abs_t;

typedef struct _dinf {
	_short type;
	int devid;
	float ABS[NdevABS];
	abs_t xABS[NdevABS];
	Atom mon;
	int attach0;
	_short reason;
//	unsigned int x,y,width,height;
//	Rotation rotation;
} dinf_t;

#define o_master 1
#define o_floating 2
#define o_absolute 4
//#define o_changed 8
#define o_raw 16
#define o_z 32
#define o_directTouch 64
#define o_kbd 128

#define NINPUT_STEP 4
dinf_t *inputs = NULL, *inputs1, *dinf, *dinf1, *dinf2, *dinf_last = NULL;
_int ninput = 0, ninput1 = 0;
#define DINF(x) for(dinf=inputs; (dinf!=dinf_last); dinf++) if (x)
#define DINF_A(x) for(dinf=inputs; (dinf!=dinf_last); dinf++) if ((dinf->type&O_absolute) && )

typedef struct _pinf {
	_short en;
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
	unsigned int x,y,x2,y2,width,height,width0,height0,width1,height1;
	unsigned long mwidth,mheight,mwidth1,mheight1;
	RROutput out;
	RRCrtc crt;
	Rotation rotation;
	_short r90;
	Atom name;
	_short connection;
	pinf_t prop[xrp_cnt];
#ifdef _BACKLIGHT
	Window win;
	_short obscured,entered;
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


static void fixMMRotation(minf_t *minf) {
	unsigned long i;
	if (minf && minf->mwidth > minf->mheight && minf->width < minf->height && minf->mheight) {
		i = minf->mwidth;
		minf->mwidth = minf->mheight;
		minf->mheight = i;
	}
}

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
	int N = abs(cnt);
	_error = 0;
	switch (target) {
	    case pr_win:
		_pr_name = "XGetWindowProperty";
		XGetWindowProperty(dpy,win,prop,pos,N,False,type,&pr_t,&pr_f,&pr_n,&pr_b,(void*)&ret);
		break;
	    case pr_out:
		_pr_name = "XRRGetOutputProperty";
		Bool pending = False;
		XRRGetOutputProperty(dpy,minf->out,prop,pos,N,False,pending,type,&pr_t,&pr_f,&pr_n,&pr_b,(void*)&ret);
		break;
	    case pr_input:
		_pr_name = "XIGetOutputProperty";
		XIGetProperty(dpy,devid,prop,0,N,False,type,&pr_t,&pr_f,&pr_n,&pr_b,(void*)&ret);
		break;
	    default:
		ERR("BUG!");
		return 0;
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
		if ((r=getProp(prop,type,data,0,cnt,chk))) return r==2;
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

static _short wGetProp(Atom prop, Atom type, void *data, long cnt, _short chk){
	target = pr_win;
	return getProp(prop,type,data,0,cnt,chk);
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
		_short i;
		for (i=0; i<NdevABS; i++) {
			const struct input_absinfo *x = libevdev_get_abs_info(device,
#if ABS_X == 0 && ABS_Y == 1
			    i);
#else
			    (i==0)?ABS_X:(i==1)?ABS_Y:ABS_Z);
#endif
			if (x && x->resolution) dinf->ABS[i] = (x->maximum - x->minimum + 0.)/x->resolution;
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
		x/=minf0.width;
		y/=minf0.height;
		h/=minf0.width;
		w/=minf0.height;
	} else {
		x/=minf0.width;
		y/=minf0.height;
		w/=minf0.width;
		h/=minf0.height;
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
	xiSetProp(aMatrix,aFloat,&matrix,9,4);
}

static void fixMonSize(minf_t *minf, double *dpmw, double *dpmh) {
	if (!minf->mheight) return;
	double _min, _max, m,  mh = minf->height1 / (minf->r90?*dpmw:*dpmh);

	// diagonal -> 4:3 height
	if (
	    (pi[p_min_native_mon] && mh < (m = (m = pf[p_min_native_mon]) < 0 ? -m : (m * (25.4 * 3 / 5)))) ||
	    (pi[p_max_native_mon] && mh > (m = (m = pf[p_max_native_mon]) < 0 ? -m : (m * (25.4 * 3 / 5))))
		) {
		m /= mh;
		*dpmw /= m;
		*dpmh /= m;
	}

	if (pi[p_max_dpi]) {
		_max = pf[p_max_dpi] / 25.4;
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
		_min = pf[p_min_dpi] / 25.4;
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

static void clearCache(){
	DINF(dinf->devid == devid) {
		memset(dinf,0,sizeof(dinf_t));
		break;
	}
}

minf_t *outputs = NULL;
int noutput = 0;

#define _BUFSIZE 1048576


static void pr_set(xrp_t prop,_short state);

#ifdef _BACKLIGHT
static void chBL1(_short obscured, _short entered) {
	if (obscured != 2) minf->obscured = obscured;
	if (entered != 2) minf->entered = entered;
	pr_set(xrp_bl,!(minf->obscured || minf->entered));
}

static void chBL(Window w, _short obscured, _short entered) {
//	MINF(minf->win == w && (minf->type&o_backlight)) {
	MINF(minf->win == w) {
		chBL1(obscured,entered);
		break;
	}
}

#if 0
#define __check_entered
static void checkEntered(){
//	DINF(o_master_ptr|o_floating) {
	DINF((dinf->type&(o_master|o_floating)) && !((dinf->type&o_kbd)) {
		Window rw,rc;
		XIButtonState b;
		XIModifierState m;
		XIGroupState g;
		double x, y, root_x = -1,root_y = -1;
		if (XIQueryPointer(dpy,devid,root,&rw,&rc,&root_x,&root_y,&x,&y,&b,&m,&g))
			MINF_T2(o_active|o_backlight,o_active|o_backlight)
			    chBL1(2,x>=minf->x && y>=minf->y && x<=minf->x2 && y<=minf->y2);
	}
}
#endif


static _short fixWin(Window win,unsigned long x,unsigned long y,unsigned long w,unsigned long h){
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

RROutput prim0, prim;

static void _pr_free(_short err){
	if (pr->en) {
		if ((err || (pi[p_safe]&2048))
		    && !XRP_EQ(pr->v,pr->vs[0]))
			xrSetProp(a_xrp[prI],type_xrp[prI],&pr->vs[0],1,0);
		pr->en = 0;
	}
	_free(pr->p);
}

static void _minf_free(){
#ifdef _BACKLIGHT
	if (minf->win) {
		XDestroyWindow(dpy,minf->win);
		minf->win = 0;
	}
#endif
	for (prI=0; prI<xrp_cnt; prI++) {
		pr = &minf->prop[prI];
		_pr_free(0);
	}
}

static _short _pr_chk(_xrp_t *v){
#ifdef _BACKLIGHT
	if (prI == xrp_bl && v->i == 0) return 1;
#endif
	XRRPropertyInfo *p = pr->p;
	int i;
	if (p->range) {
		return (p->num_values == 2 && type_xrp[prI] == XA_INTEGER && *(prop_int*)v >= p->values[0] && *(prop_int*)v <= p->values[1]);
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
	} else if (pinf) XFree(pinf);
	return 0;
}

static void _pr_get(_short r);
static void _pr_set(_short state){
	_xrp_t v;
	v = pr->vs[state];
//	if (type_xrp[prI] == XA_ATOM && !v.a) return;
	if (xrevent1 < 0) {
		_xrPropFlush();
		_pr_get(0);
	}
	if (!pr->en || XRP_EQ(v,pr->v)) return;
//	if (type_xrp[prI] == XA_ATOM) DBG("set %s %s -> %s",natom(0,minf->name),natom(1,pr->v.a),natom(2,v.a));
	pr->v1 = pr->v;
	pr->v = v;
	xrSetProp(a_xrp[prI],type_xrp[prI],&v,1,0);
}

static void pr_set(xrp_t prop,_short state){
	pr = &minf->prop[prI = prop];
	if (pr->en)
	    _pr_set(state);
}

static void _pr_get(_short r){
	switch (prI) {
	    case xrp_non_desktop: break;
#ifdef _BACKLIGHT
	    case xrp_bl:
		if (!(pi[p_safe]&4096)) return;
		break;
#endif
	    default:
		if (!val_xrp[prI].a) return;
	    	break;
	}
	Atom prop = a_xrp[prI];
	Atom save = a_xrp_save[prI];
	Atom type = type_xrp[prI];
	_short _save = !(pi[p_safe]&2048);
	_short _init = __init && !pr->en;
	_xrp_t v;
	if (!xrGetProp(prop,type,&v,1,0)) goto err;
//	if (type == XA_ATOM) DBG("get %s %s = %s",natom(0,minf->name),natom(1,prop),natom(2,v.a));
//	if (type == XA_INTEGER) DBG("get %s %s = %i",natom(0,minf->name),natom(1,prop),v.i);
	_short inf = _pr_inf(prop);
	if (inf && pr->en && r != 2) {
		// new default/saved or nothing new
		if (XRP_EQ(v,pr->v)) return;
		if (XRP_EQ(v,pr->v1)) return;
		pr->v = v;
		if (!_pr_chk(&v)) goto err;
		pr->vs[0] = v;
		if (_save)
		    xrSetProp(save,type,&v,1,0); // MUST be ok
		return;
	}
	pr->v = v;
	if (!pr->p || pr->p->num_values < 2 || !_pr_chk(&val_xrp[prI]) || !_pr_chk(&v)) goto err;
	if (!pr->en) {
		pr->vs[0] = pr->v1 = v;
		pr->vs[1] = val_xrp[prI];
		switch (prI) {
#ifdef _BACKLIGHT
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
    xrrSet();
    int nout = -1, active = 0, non_desktop = 0, non_desktop0 = 0, non_desktop1 = 0;
    XRRCrtcInfo *cinf = NULL;
    XRROutputInfo *oinf = NULL;
    i = xrrr ? xrrr->noutput : 0;
    if (noutput != i) {
	if (outputs) {
		for(minf=outputs; (minf!=minf_last); minf++) _minf_free();
		free(outputs);
	}
	prim0 = prim = 0;
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
	if (minf->mwidth != oinf->mm_width || minf->mheight != oinf->mm_height) {
		minf->mwidth1 = minf->mwidth = oinf->mm_width;
		minf->mheight1 = minf->mheight = oinf->mm_height;
		minf->type |= o_changed;
	}
	if (minf->mwidth && minf->mheight) minf->type |= o_msize;
	//xrGetRangeProp(xrp_bl,&minf->bl);
#ifdef _BACKLIGHT
	if (minf->prop[xrp_bl].en) minf->type |= o_backlight;
#endif
	if (!(minf->crt = oinf->crtc) || !(cinf=XRRGetCrtcInfo(dpy, xrrr, minf->crt)))
		continue;
	minf->width = minf->width0 = cinf->width;
	minf->height = minf->height0 = cinf->height;
	if (minf->x != cinf->x || minf->y != cinf->y || minf->rotation != cinf->rotation) {
		minf->x = cinf->x;
		minf->y = cinf->y;
		minf->rotation = cinf->rotation;
		minf->type |= o_changed;
	}
	XRRModeInfo *m,*m0 = NULL,*m1 = NULL, *m0x = NULL;
	RRMode id = cinf->mode;
	// find precise current mode
	for (i=0; i<xrrr->nmode; i++) {
		m = &xrrr->modes[i];
		if (m->id == id) {
			m1 = m;
			break;
		}
	}
	int j;
	// find first preferred simple: m0
	// and with size: m0x
	for (j=0; j<oinf->npreferred && !m0x; j++) {
		id = oinf->modes[j];
		for (i=0; i<xrrr->nmode && !m0x; i++) {
			m = &xrrr->modes[i];
			if (m->id != id) continue;
			if (m->width && m->height) m0x = m;
			else if (!m0) m0 = m;
		}
	}
	if (pi[p_safe]&256) m0 = m1;
	else if (m0x) m0 = m0x;
	if (m1) {
		minf->width = m1->width;
		minf->height = m1->height;
	}
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
	unsigned int x2 = minf->x + minf->width - 1;
	unsigned int y2 = minf->y + minf->height - 1;
	if (x2 != minf->x2 || y2 != minf->y2) {
		minf->x2 = x2;
		minf->y2 = y2;
		minf->type |= o_changed;
	}
	if (minf->width && minf->height) minf->type |= o_size;
    }
    MINF_T(o_changed) {
	oldShowPtr |= 16;
	DINF(dinf->mon && dinf->mon == minf->name) dinf->type |= o_changed;
    }
    if (!active && non_desktop)
	MINF_T(o_non_desktop) pr_set(xrp_non_desktop,1);
    if (active && non_desktop1)
	MINF(minf->prop[xrp_non_desktop].en && minf->prop[xrp_non_desktop].vs[0].i == 1)
	    pr_set(xrp_non_desktop,0);
#ifdef _BACKLIGHT
    if ((pi[p_safe]&4096)) {
	MINF_T2(o_active|o_backlight,o_active|o_backlight) {
		if (!minf->win) {
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
//    _ungrabX(); // forceUngrab() instead
}

static void maps_to_all(_short all){
	DINF(dinf->mon) MINF(minf->name == dinf->mon) {
//		if (!(minf->type&o_active)) break
		if (!all && !((dinf->type|minf->type)&o_changed)) break;
		if ((dinf->type&o_changed)) dinf->type ^= o_changed;
		if ((minf->type&o_changed)) minf->type ^= o_changed;
		map_to();
		break;
	}
}

static _short  setScrSize(int dpi100x,int dpi100y){
	unsigned long px=0, py=0, py1=0, mpx = 0, mpy = 0;
	MINF_T(o_active) {
		px = _max(minf->x + minf->width, px);
		py += minf->height;
		py1 = _max(minf->y + minf->height, py1);
		mpx = _max(minf->mwidth,mpx);
		mpy += minf->mheight;
	}
	py = _max(py,py1);
	if (dpi100x) mpx = 25.4*100*px/dpi100x;
	if (dpi100y) mpy = 25.4*100*py/dpi100y;
	if (minf0.width != px || minf0.height != py || minf0.mwidth != mpx || minf0.mheight != mpy) {
		DBG("set screen size %lux%lu / %lux%lumm - %.1f %.1fdpi",px,py,mpx,mpy,25.4*px/mpx,25.4*py/mpy);
		XFlush(dpy);
		XSync(dpy,False);
		XRRSetScreenSize(dpy,root,px,_max(py,py1),mpx,mpy);
		XFlush(dpy);
		XSync(dpy,False);
		if (_error) return 0;
		minf0.width = px;
		minf0.height = py;
		minf0.mwidth = mpx;
		minf0.mheight = mpy;
		//xrPropFlush();
		maps_to_all(1);
	}
	return 1;
}

unsigned int pan_x,pan_y,pan_cnt;
static void _pan(minf_t *m) {
	if (pi[p_safe]&64 || !m || !m->crt) return;
	XRRPanning *p = XRRGetPanning(dpy,xrrr,m->crt), p1;
	if (!p) return;
	memset(&p1,0,sizeof(p1));
	p1.top = pan_y;
	p1.width = m->width1;
	p1.height = m->height1;
	if (p1.width != p->width || p1.height != p->height || p1.top != p->top ||
		(p->left|p->track_width|p->track_height|p->track_left|p->track_top|p->border_left|p->border_top|p->border_right|p->border_bottom)) {
		DBG("crtc %lu panning %ix%i+%i+%i/track:%ix%i+%i+%i/border:%i/%i/%i/%i -> %ix%i+%i+%i/{0}",
			m->crt,p->width,p->height,p->left,p->top,  p->track_width,p->track_height,p->track_left,p->track_top,  p->border_left,p->border_top,p->border_right,p->border_bottom,
			p1.width,p1.height,p1.left,p1.top);
		if (XRRSetPanning(dpy,xrrr,m->crt,&p1)==Success) {
			pan_cnt++;
			m->type |= o_changed;
//			XFlush(dpy);
//			XSync(dpy,False);
		}
	}
	XRRFreePanning(p);
	pan_x = _max(pan_x, m->width);
	pan_y += m->height;
}


#ifdef XSS
static void _monFS(xrp_t p, _short st){
	if (val_xrp[p].a) pr_set(p,st);
}

static void monFullScreen(){
	if (!val_xrp[xrp_ct].a && !val_xrp[xrp_cs].a) return;
	if (noXSS) {
		wa.x = -1;
		wa.y = -1;
		getGeometry();
	}
	MINF(minf->out) {
		_short st = noXSS && (minf->type&o_active) && wa.x>=minf->x && wa.x<=minf->x2 && wa.y>=minf->y && wa.y<=minf->y2;
		_monFS(xrp_ct,st);
		_monFS(xrp_cs,st);
	}
	xrPropFlush();
}
#endif

static _short findResDev(){
	if (resDev) DINF(dinf->devid == resDev) {
		dinf2 = dinf;
		MINF(minf->name == dinf->mon) {
			if (minf2 == minf) break;
			minf2 = minf;
			return 0;
		}
		return 1;
	}
	minf2 = NULL;
	dinf2 = NULL;
	return 0;
}

static void fixGeometry(){
	if (findResDev() && resDev == devid) return;
	if (minf2) goto find1;
	DINF((dinf->type&o_directTouch)) {
		MINF((minf->type&o_active) && dinf->mon == minf->name) {
			minf2 = minf;
			goto find1;
		}
	}
find1:
	minf1 = NULL;
	_int nm = 0;
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
		double dpmh, dpmw;
		pan_x = pan_y = pan_cnt = 0;
		if (do_dpi) {
			dpmh = (.0 + minf1->height) / minf1->mheight;
			dpmw = (.0 + minf1->width) / minf1->mwidth;
			// auto resolution usual equal physical propotions
			// in mode resolution - use one max density
		}
		if (minf1 && minf1->crt != minf2->crt) _pan(minf1);
		if (nm != ((minf1 && minf1->crt != minf2->crt) + (!!minf2->crt)))
		    MINF_T(o_active) {
			if (minf->crt != minf1->crt && minf->crt != minf2->crt) {
				if (do_dpi && minf->mwidth && minf->mheight) {
					if ( !(minf1 && minf->width == minf1->width && minf->height == minf1->height && minf->mwidth == minf1->mwidth && minf->mheight == minf1->mheight) &&
					     !(minf2 && minf->width == minf2->width && minf->height == minf2->height && minf->mwidth == minf2->mwidth && minf->mheight == minf2->mheight))
						fixMonSize(minf, &dpmw, &dpmh);
				}
				_pan(minf);
			}
		}
		_pan(minf2);

//		xrPropFlush();

		if (!pan_x) pan_x = minf0.width;
		if (!pan_y) pan_y = minf0.height;

		if (do_dpi) {
			if (minf2->crt && minf1->crt != minf2->crt && minf2->crt) fixMonSize(minf2, &dpmw, &dpmh);
			fixMonSize(minf1, &dpmw, &dpmh);
			if (!(pi[p_safe]&512))
				if (dpmh > dpmw) dpmw = dpmh;
				else dpmh = dpmw;
				if (setScrSize(dpmw*25.4*100,dpmh*25.4*100)) goto ex;
		} else if (!pan_cnt) goto ex;
		setScrSize(0,0);
	}
ex:
	xrrFree();
	if (pan_cnt) maps_to_all(0);
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
_int tdevs = 0, tdevs2=0;
//#define floating (pi[p_floating]==1)


_int ninput1_;
XIDeviceInfo *d2;
// remember only devices on demand
static void _add_input(_short t){
	if (dinf1) goto ex;
	_int i = ninput1_++;
	if (ninput1_ > ninput1 || !inputs1) {
		_int n = i + NINPUT_STEP;
		dinf_t *p = malloc(n*sizeof(dinf_t));
		if (inputs1) {
			memcpy(p,inputs1,i*sizeof(dinf_t));
			free(inputs1);
		}
		memset(&p[i],0,NINPUT_STEP*sizeof(dinf_t));
		inputs1 = p;
		ninput1 = n;
	}
	dinf1 = &inputs1[i];
	DINF(dinf->devid == devid) {
		*dinf1 = *dinf;
		dinf1->type = t; // reset
		return;
	}
	dinf1->devid = devid;
	dinf1->attach0 = d2->attachment;
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
			minf->mwidth = dinf->ABS[!minf->r90];
			minf->mheight = dinf->ABS[minf->r90];
			minf->mwidth1 = dinf->ABS[0];
			minf->mheight1 = dinf->ABS[1];
			minf->type |= o_changed;
			dinf->type |= o_changed;
		}
	}
	maps_to_all(0);
}

static void getHierarchy(){
	inputs1 = NULL;
	ninput1_ = 0;
	
	int i,j,ndevs2,nkbd,m=0;
	void *mTouch = useRaw ? &ximaskTouchRaw : &ximaskTouch;

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
		_short t = 0, scroll = 0, raw = 0;
		_short type = 0;
		busy = 0;
		if (__init) {
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
#ifdef __check_entered
			type|=o_floating;
#endif
			break;
		    case XISlavePointer:
			if (!strstr(d2->name," XTEST ")) break;
			if (d2->attachment == cr.return_pointer) xtestPtr0 = devid;
			else if (m && d2->attachment == m) xtestPtr = devid;
			continue;
//		    case XISlaveKeyboard:
//			if (strstr(d2->name," XTEST ")) continue;
//			type |= o_kbd;
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
		for (j=0; j<d2->num_classes; j++) {
			XIAnyClassInfo *cl = d2->classes[j];
			switch (cl->type) {
#undef e
#define e ((XITouchClassInfo*)cl)
			    case XITouchClass:
				if (e->mode != XIDirectTouch) break;
				t=1;
				break;
#undef e
#define e ((XIValuatorClassInfo*)cl)
			    case XIValuatorClass:
				switch (e->mode) {
//				    case Relative:
//					if (d2->use==XIMasterPointer) break; // simpler later
//					rel=1;
//					break;
				    case Absolute: 
					//DBG("valuator abs: '%s' '%s' %i %f,%f %i",d2->name,natom(0,e->label),e->number,e->min,e->max,e->resolution);
					if (d2->use==XIMasterPointer) break; // simpler later
					type|=o_absolute;
					_add_input(type);
					if (e->number >= 0 && e->number <= NdevABS && e->max > e->min) {
						dinf1->xABS[e->number].min = e->min;
						dinf1->xABS[e->number].max = e->max;
						dinf1->xABS[e->number].resolution = e->resolution;
						raw |= 1 << e->number;
					}
					break;
				}
				break;
#undef e
#define e ((XIScrollClassInfo*)cl)
			    case XIScrollClass:
				scroll=1;
				//type|=o_scroll;
				break;
			}
		}
		if (devid ==2 ) ERR("NO xi device %i '%s' raw transformation unknown. broken drivers? %i",devid,d2->name,type);
		if (pa[p_device] && *pa[p_device]) {
			if (!strcmp(pa[p_device],d2->name) || pi[p_device] == devid) {
				t = 1;
				scroll = 0;
			} else {
				scroll = 1;
				//type|=o_scroll;
				type &= ~(_short)o_directTouch;
				//t = 0;
				goto skip_map;
			}
		}
skip_map:

		if (t && !scroll) type|=o_directTouch;
		if (type&(o_master|o_kbd)) continue;
		else if (raw == 7) type |= o_raw|o_z;
		else if (raw == 3) type |= o_raw;
		else if ((type&(o_absolute|o_directTouch)) && mTouch != &ximaskTouch) {
			mTouch = &ximaskTouch;
			ERR("xi device %i '%s' raw transformation unknown. broken drivers?",devid,d2->name);
		}
		tdevs+=t;
		// exclude touchscreen with scrolling
		void *c = NULL;
		cf.deviceid = ca.deviceid = ximask[0].deviceid = devid;
		ximask[0].mask = NULL;
		switch (d2->use) {
		    case XIFloatingSlave:
			if (!(type&o_directTouch)) break;
			switch (pi[p_floating]) {
			    case 1:
				tdevs2++;
				break;
			    case 0:
				if (m) c = &ca;
				break;
			    case 2:
				tdevs2++;
				if (m) {
					c = &ca;
					ximask[0].mask=(void*)ximask0;
				} else {
					ximask[0].mask=mTouch;
				}
				break;
			}
			break;
		    case XISlavePointer:
			if (!t) ximask[0].mask = (void*)(showPtr ? ximask0 : ximaskButton);
			else if (scroll) ximask[0].mask = (void*)(showPtr ? mTouch : ximask0);
			else {
				tdevs2++;
				switch (pi[p_floating]) {
				    case 1:
					ximask[0].mask=mTouch;
					XIGrabDevice(dpy,devid,root,0,None,XIGrabModeSync,XIGrabModeTouch,False,ximask);
//					XIGrabTouchBegin(dpy,devid,root,0,ximask,0,NULL);
					ximask[0].mask=NULL;
					break;
				    case 0:
					if (m && m != d2->attachment) c = &ca;
					break;
				    case 2:
					if (d2->attachment == cr.return_pointer) c = &cf;
					break;
				}
			}
			break;
//		    case XISlaveKeyboard:
//			nkbd++;
//		    default:
//			break;
		}
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
		if (type) _add_input(type);
		if (c) XIChangeHierarchy(dpy, c, 1);
		if (ximask[0].mask) XISelectEvents(dpy, root, ximask, Nximask);
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
	XIFreeDeviceInfo(info2);
	//_ungrabX();
	if (!__init) forceUngrab();
	if (pi[p_safe]&8) XFixesShowCursor(dpy,root);
	tdevs -= tdevs2;

	if (inputs) free(inputs);
	inputs = inputs1;
	ninput = ninput1 = ninput1_;
	dinf_last = &inputs[ninput];


#ifdef USE_EVDEV
	DINF((dinf->type&o_absolute) && !(dinf->ABS[0]>0) && !(dinf->ABS[1]>0)) {
		getEvRes();
	}
#endif
	// drivers are unsafe, evdev first
	DINF((dinf->type&o_absolute) && !(dinf->ABS[0]>0) && !(dinf->ABS[1]>0) && dinf->xABS[0].resolution>=1000) {
		_short i;
		for (i=0;i<NdevABS;i++) {
			if (dinf->xABS[i].resolution>1000) dinf->ABS[i] = (dinf->xABS[0].max - dinf->xABS[0].min)/(dinf->xABS[i].resolution/1000.);
		}
	}


	touchToMon();
	devid = 0;
	fixGeometry();
	xrPropFlush();

	__init = 0;
}

static void _signals(void *f){
	static int sigs[] = {
		SIGINT,
		SIGABRT,
		SIGQUIT,
		SIGHUP,
		0,
		};
	int *s;
	for(s=sigs; *s; s++) signal(*s,f);
}

static void _eXit(int sig){
	if (sig == SIGHUP) {
		oldShowPtr |= 2|8;
		return;
	}
	_signals(SIG_DFL);
	if (!grabcnt) XGrabServer(dpy);
	grabcnt = 0;

	MINF(minf->out) {
		for (prI=0; prI<xrp_cnt; prI++) {
			pr = &minf->prop[prI];
			if (!pr->en) continue;
			if (!XRP_EQ(pr->v,pr->vs[0])) {
				_pr_set(0);
				_quit = 1;
			}
			if (!(pi[p_safe]&2048)) {
				XRRDeleteOutputProperty(dpy,minf->out,a_xrp_save[prI]);
				_quit = 1;
			}
		}
	}

	int m = 0;
	if (cr.return_pointer && ca.new_master && cr.return_pointer != ca.new_master) {
		cr.deviceid = ca.new_master;
		cr.return_mode = XIAttachToMaster;
		if (XIChangeHierarchy(dpy, (void*)&cr, 1) == Success) {
			m = ca.new_master;
			_quit = 1;
		}
	}
	DINF(dinf->attach0) {
		ca.new_master = dinf->attach0;
		ca.deviceid = dinf->devid;
		if (XIChangeHierarchy(dpy, (void*)&ca, 1) == Success) _quit = 1;
	}
	if (!_quit) exit(0);
	XFlush(dpy);
	win = win1 = root;
	noXSS = noXSS1 = 0;
}

static void setShowCursor(){
    	switch (oldShowPtr&0xf8) {
	    case (8|16):
	    case 8:
		oldShowPtr ^= 8;
		xrMons0();
	    case 16:
		switch (oldShowPtr^showPtr) {
		    case 16:
			touchToMon();
			devid = 0;
			fixGeometry();
			xrPropFlush();
		    case 0:
			xrrFree();
			oldShowPtr = showPtr;
			return;
		}
	}
	oldShowPtr = showPtr;
	getHierarchy();
	if ((oldShowPtr & 2) && !showPtr) return;
	// "Hide" must be first!
	// but prefer to filter error to invisible start
	if (showPtr != curShow || showPtr) {
		if ((curShow = showPtr)) XFixesShowCursor(dpy, root);
		else XFixesHideCursor(dpy, root);
	}
}

static void _set_bmap(_int g, _short i, _int j){
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
	_int i,j;
	for(i=1; i<8; i++){
		_int g = 0;
		_int j3 = 0;
		for (j=1; j<=pi[p_maxfingers]; j++) {
			j3 += 3;
			g = (g<<3)|i;
			if (i<4 ? (j==1) : (j>=pi[p_minfingers])) _set_bmap(g,i,j3);
			if (j==1) continue;
			if (i==BUTTON_DND) continue;
			if (i<4 ? (j>2) : ((j-1)<pi[p_minfingers])) continue;
//			_int g1 = BUTTON_DND, g2 = 7, k;
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
	minf0.width = DisplayWidth(dpy,screen);
	minf0.height = DisplayHeight(dpy,screen);
	minf0.mwidth = DisplayWidthMM(dpy,screen);
	minf0.mheight = DisplayHeightMM(dpy,screen);
	fixMMRotation(&minf0);
	if (minf != minf_last && memcmp(&minf0,&m,sizeof(m))) {
		maps_to_all(1);
		oldShowPtr |= 8;
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
			oldShowPtr |= 2;
			showPtr = 1;
		}
		//return 0;
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
		oldShowPtr |= 2|8;
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
#ifdef _BACKLIGHT
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
#endif

	XISetMask(ximaskButton, XI_ButtonPress);
	XISetMask(ximaskButton, XI_ButtonRelease);
	XISetMask(ximaskButton, XI_Motion);

	XISetMask(ximaskTouch, XI_TouchBegin);
	XISetMask(ximaskTouch, XI_TouchUpdate);
	XISetMask(ximaskTouch, XI_TouchEnd);

	XISetMask(ximaskTouchRaw, XI_RawTouchBegin);
	XISetMask(ximaskTouchRaw, XI_RawTouchUpdate);
	XISetMask(ximaskTouchRaw, XI_RawTouchEnd);

	if (pi[p_floating]==3) {
		if (useRaw) {
			XISetMask(ximask0, XI_RawTouchBegin);
			XISetMask(ximask0, XI_RawTouchUpdate);
			XISetMask(ximask0, XI_RawTouchEnd);
		} else {
			XISetMask(ximask0, XI_TouchBegin);
			XISetMask(ximask0, XI_TouchUpdate);
			XISetMask(ximask0, XI_TouchEnd);
		}
	}

	XISetMask(ximaskTouch, XI_PropertyEvent);
	XISetMask(ximask0, XI_PropertyEvent);

	XISetMask(ximask0, XI_HierarchyChanged);
#ifdef DEV_CHANGED
	XISetMask(ximask0, XI_DeviceChanged);
#endif
	XISelectEvents(dpy, root, ximask, Nximask);
	XIClearMask(ximask0, XI_HierarchyChanged);
#ifdef DEV_CHANGED
	XIClearMask(ximask0, XI_DeviceChanged);
#endif

	_grabX();
	if (pf[p_res]<0 || mon || mon_sz) {
//		if (xrr = XRRQueryExtension(dpy, &xrevent, &ierr)) {
		// need xropcode
		if (xrr = XQueryExtension(dpy, "RANDR", &xropcode, &xrevent, &ierr)) {
			int m = RRScreenChangeNotifyMask;
#ifdef RROutputPropertyNotifyMask
			int v1 = 0, v2 = 0;
			XRRQueryVersion(dpy,&v1,&v2);
			if (v1 > 1 || (v1 == 1 && v2 > 1)) {
				xrevent1 = xrevent + RRNotify;
				m |= RRCrtcChangeNotifyMask|RROutputChangeNotifyMask|RROutputPropertyNotifyMask;
			}
#endif

			xrevent += RRScreenChangeNotify;
			XRRSelectInput(dpy, root, m);
		};
	}
	_scr_size();
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
	if (!(pi[p_safe]&64)) fixGeometry();
	else
#endif
	    findResDev();
	if (dinf2) return 1;
	resDev = 0;
	return 0;
}
#endif

int main(int argc, char **argv){
#ifdef XTG
	_short i;
	int opt;
	Touch *to;
	double x1,y1,x2,y2,xx,yy,res;
	_short z1,z2;
	_short end,tt,g,bx,by;
	_int k;
	TouchTree *m;
	_int vl;
	int detail;

	while((opt=getopt(argc, argv, pc))>=0){
		for (i=0; i<MAX_PAR && pc[i<<1] != opt; i++);
		if (i>=MAX_PAR) {
			printf("Usage: %s {<option>} {<value>:<button>[:<key>]}\n",argv[0]);
			for(i=0; i<MAX_PAR; i++) {
				printf("	-%c ",pc[i<<1]);
				if (pf[i] == pi[i]) printf("%i",pi[i]);
				else printf("%.1f",pf[i]);
				printf(" %s\n",ph[i]);
			}
			printf("<value> is octal combination, starting from x>0, where x is last event type:\n"
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
		if (!optarg) continue;
		pa[i] = optarg;
		pf[i] = atof(optarg);
		pi[i] = atoi(optarg);
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
	resX = resY = pf[p_res] < 0 ? 1 : pf[p_res];
	strict = pa[p_mon] &&  pa[p_mon][0] == '-' && !pa[p_mon][1];
	mon_sz = pi[p_mon] < 0;
	mon_grow = -pf[p_mon];
	if (!strict && !mon_sz && pa[p_mon] && *pa[p_mon])
		mon = XInternAtom(dpy, pa[p_mon], False);
	resXY = !mon && pf[p_res]<0;
	if (pi[p_floating]&4) {
		useRaw = 1;
		pi[p_floating] ^= 4;
	}
#endif
	if (!dpy) return 1;
	init();
	printGrp();
	getPropWin1();
#ifdef XTG
	while (!_quit) {
#else
	while (1) {
#endif
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
		forceUngrab();
#endif
		XNextEvent(dpy, &ev);
//		DBG("ev %i",ev.type);
		switch (ev.type) {
#ifdef XTG
		    case GenericEvent:
			if (ev.xcookie.extension == xiopcode) {
//				DBG("ev %i",ev.xcookie.evtype);
#undef e
#define e ((XIDeviceEvent*)ev.xcookie.data)
				switch (ev.xcookie.evtype) {
				    case XI_TouchBegin:
				    case XI_TouchUpdate:
				    case XI_TouchEnd:
					if (!tdevs2 || !xiGetE()) goto ev;
					devid = e->deviceid;
					if (resDev != devid && !chResDev()) goto evfree;
					detail = e->detail;
					end = (ev.xcookie.evtype == XI_TouchEnd
					    || (e->flags & XITouchPendingEnd));
					x2 = e->root_x;
					y2 = e->root_y;
					if ((dinf->type&o_z) && e->valuators.mask_len && (e->valuators.mask[0]&7)==7) {
						abs_t *a = &dinf2->xABS[2];
						z2 = (e->valuators.values[2] - a->min)*99/(a->max - a->min) + 1;
					} else z2 = 0;
					xiFreeE();
					break;
#undef e
#define e ((XIRawEvent*)ev.xcookie.data)
				    case XI_RawTouchBegin:
				    case XI_RawTouchUpdate:
				    case XI_RawTouchEnd:
					if (!tdevs2 || !xiGetE()) goto ev;
					devid = e->deviceid;
					if ((resDev != devid && !(chResDev() && minf2 && (dinf->type&o_raw)))
					    || !e->valuators.mask_len
					    || (e->valuators.mask[0]&3)!=3)
						goto evfree;
					detail = e->detail;
					end = (ev.xcookie.evtype == XI_RawTouchEnd
					    || (e->flags & XITouchPendingEnd));
#define _raw(i) e->raw_values[i]
//#define _raw(i) e->valuators.values[i]
					abs_t *a = &dinf2->xABS[0];
					x2 = (_raw(0) - a->min)*minf2->width1/(a->max - a->min);
					a = &dinf2->xABS[1];
					y2 = (_raw(1) - a->min)*minf2->height1/(a->max - a->min);
					double xx = x2;
					switch (minf2->rotation) {
					    case RR_Rotate_0:
						x2 += minf2->x;
						y2 += minf2->y;
					    case RR_Rotate_90:
						x2 = minf2->width1 - y2 + minf2->y;
						y2 = xx + minf2->x;
						break;
					    case RR_Rotate_180:
						x2 = minf2->width1 - x2 + minf2->x;
						y2 = minf2->height1 - y2 + minf2->y;
						break;
					    case RR_Rotate_270:
						x2 = y2 + minf2->y;
						y2 = minf2->height1 - xx +  + minf2->x;
						break;
					}
					if ((dinf->type&o_z) && (e->valuators.mask[0]&4)) {
						a = &dinf2->xABS[2];
						z2 = (_raw(2) - a->min)*99/(a->max - a->min) + 1;
					} else z2 = 0;
					xiFreeE();
					break;

#ifdef DEV_CHANGED
				    case XI_DeviceChanged:
#undef e
#define e ((XIDeviceChangedEvent*)ev.xcookie.data)
					if (xiGetE()) {
						int r = e->reason;
						DBG("dev changed %i %i (%i)",r,e->deviceid,devid);
						xiFreeE();
						if (r == XISlaveSwitch) goto ev2;
					}
					oldShowPtr |= 2;
					continue;
#endif
#undef e
#define e ((XIPropertyEvent*)ev.xcookie.data)
				    case XI_PropertyEvent:
					if (xiGetE()) {
						Atom p = e->property;
						devid = e->deviceid;
						xiFreeE();
						if (
							//devid!=devid1 ||
							p==aMatrix
							|| p==aABS
							) goto ev2;
						if (p==aDevMon) {
							if (mon) goto ev2;
							clearCache();
						}
					}
#undef e
#define e ((XIHierarchyEvent*)ev.xcookie.data)
#if 0
					oldShowPtr |= 2; continue;
				    case XI_HierarchyChanged:
					if (xiGetE()) {
						if (e->flags&(XISlaveRemoved|XISlaveAdded)) oldShowPtr |= 2;
						xiFreeE();
					}
#else
				    case XI_HierarchyChanged:
					oldShowPtr |= 2;
#endif
					continue;
#ifdef _BACKLIGHT
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
							chBL(e->event,2,ev.xcookie.evtype==XI_Enter);
							break;
						}
						xiFreeE();
					}
					continue;
#endif
#undef e
#define e ((XIDeviceEvent*)ev.xcookie.data)
				    case XI_ButtonPress:
				    case XI_ButtonRelease:
					showPtr = 1;
					timeHold = 0;
					goto ev2;
				    default:
					showPtr = 1;
					timeHold = 0;
					goto ev2;
				}
				x2 += pf[p_round];
				y2 += pf[p_round];
				showPtr = 0;
//				fprintf(stderr,"ev2 %i %i\n",e->deviceid,ev.xcookie.evtype);
				res = resX;
				tt = 0;
				m = &bmap;
				g = minf2 ? ((int)x2 <= minf2->x) ? BUTTON_RIGHT : ((int)x2 >= minf2->x2) ? BUTTON_LEFT : ((int)y2 <= minf2->y) ? BUTTON_UP : ((int)y2 >= minf2->y2) ? BUTTON_DOWN : 0 : 0;
				if (g) tt |= PH_BORDER;
				for(i=P; i!=N; i=TOUCH_N(i)){
					Touch *t1 = &touch[i];
					if (t1->touchid != detail || t1->deviceid != devid) continue;
					to = t1;
					goto tfound;
				}
				if (oldShowPtr && N!=P) {
					ERR("Drop %i touches",TOUCH_CNT);
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
				z1 = to->z;
				x1 = to->x;
				y1 = to->y;
				switch (to->g) {
				    case BAD_BUTTON: goto skip;
				    case BUTTON_DND: goto next_dnd;
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
					XTestFakeMotionEvent(dpy,screen,x2,y2,0);
					if (end) XTestFakeButtonEvent(dpy,BUTTON_DND,0,0);
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
					//fprintf(stderr,"button %i\n",g);
				}
//				XFlush(dpy);
				to->tail = xx;
				TIME(to->time,T);
				to->z = z2;
				to->x = x2;
				to->y = y2;
skip:
				if (!end) goto ev2;
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
#endif
#ifdef XSS
#undef e
#define e (ev.xclient)
		    case ClientMessage:
			// no time
			if (e.message_type == aWMState){
				if (e.window==win)
					WMState(&e.data.l[1],e.data.l[0]);
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
		    case DestroyNotify:
			if (win1 == ev.xany.window) win1 = None;
			break;
/*
		    case UnmapNotify:
		    case MapNotify:
		    case ReparentNotify:
			goto ev;
			break;
*/
#ifdef XTG
#ifdef _BACKLIGHT
#undef e
#define e (ev.xvisibility)
		    case VisibilityNotify:
			chBL(e.window,e.state != VisibilityUnobscured,2);
			goto ev;
#if 0
#undef e
#define e (ev.xcrossing)
		    case EnterNotify:
		    case LeaveNotify:
			//TIME(T,e.time);
			chBL(e.window,2,ev.type == EnterNotify);
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
#ifndef MINIMAL
				if (!xrr) {
					minf0.width = e.width;
					minf0.height = e.height;
					fixMMRotation(&minf0);
					oldShowPtr |= 8;
				}
#endif
				break;
			}
#ifdef _BACKLIGHT
			if (e.override_redirect) {
			    if (fixWin(e.window,e.x,e.y,e.width,e.height)) break;
			}
#endif
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
#undef e
#define e ((XRRScreenChangeNotifyEvent*)&ev)
			if (ev.type == xrevent) {
				// RANDR time(s) is static
				//TIME(T,e->timestamp > e->config_timestamp ? e->timestamp : e->config_timestamp);
				XRRUpdateConfiguration(&ev);
				_scr_size();
				oldShowPtr |= 8;
				//minf0.rotation = e->rotation;
				break;
			}
#ifdef RROutputPropertyNotifyMask
#undef e
#define e ((XRRNotifyEvent*)&ev)
			if (ev.type == xrevent1) {
				// RANDR time(s) is static
				//XRRTimes(dpy,screen,&T);
				switch (e->subtype) {
#ifndef MINIMAL
#undef e
#define e ((XRROutputPropertyNotifyEvent*)&ev)
				    case RRNotify_OutputProperty:
					pr = NULL;
					MINF(minf->out == e->output) {
						for (prI = 0; prI<xrp_cnt; prI++) {
							if (e->property == a_xrp_save[prI]) goto ev2;
							if (e->property != a_xrp[prI]) continue;
							if (prI == xrp_non_desktop) oldShowPtr |= 8;
							pr = &minf->prop[prI];
							if (XRP_EQ(pr->v,pr->v1)) break;
							pr->v1 = pr->v;
							goto ev2;
						}
						break;
					}
					if (!pr) goto ev2;
					if (e->state) goto ev2;
					_xrPropFlush();
					_pr_get(0);
					xrPropFlush();
					break;
#endif
				    default:
					oldShowPtr |= 8;
				}
				break;
			}
			
#endif
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
	XCloseDisplay(dpy);
#endif
}
