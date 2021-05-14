/*
	xtg v1.37 - per-window keyboard layout switcher [+ XSS suspend].
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
int xkbEventType=-100, xkbError, n1, revert;
XEvent ev;
unsigned char *ret;

// min/max: X11/Xlibint.h
static inline long _min(long x,long y){ return x<y?x:y; }
static inline long _max(long x,long y){ return x>y?x:y; }


#ifdef XTG
//#define DEV_CHANGED
int devid = 0;
int xiopcode=0, xfopcode=0, xfevent=-100;
Atom aFloat,aMatrix,aABS,aDevMon,aNonDesk,aNonDeskSave;
XRRScreenResources *xrrr = NULL;
_short showPtr, oldShowPtr = 0, curShow;
#ifdef _BACKLIGHT
Atom aBl,aBlSave;
#endif
int floatFmt = sizeof(float) << 3;
int atomFmt = sizeof(Atom) << 3;
int winFmt = sizeof(Window) << 3;
#define prop_int int32_t
int intFmt = sizeof(prop_int) << 3;
#ifdef USE_EVDEV
Atom aNode;
#endif
#ifdef XSS
Atom aCType, aCType1, aCTFullScr = 0;
Atom aColorspace, aColorspace1, aCSFullScr = 0;
XWindowAttributes wa;
#endif

#define MASK_LEN XIMaskLen(XI_LASTEVENT)
typedef unsigned char xiMask[MASK_LEN];
xiMask ximaskButton = {}, ximaskTouch = {}, ximask0 = {};
xiMask ximaskRoot = {};
int Nximask = 1;
XIEventMask ximask[] = {{ .deviceid = XIAllDevices, .mask_len = MASK_LEN, .mask = (void*)&ximask0 },
			 { .deviceid = XIAllMasterDevices, .mask_len = MASK_LEN, .mask = (void*)&ximaskRoot }};
//#define MASK(m) ((void*)ximask[0].mask=m)
#ifdef _BACKLIGHT
xiMask ximaskWin0 = {};
XIEventMask ximaskWin[] = {{ .deviceid = XIAllDevices, .mask_len = MASK_LEN, .mask = (void*)&ximaskWin0 },
			 { .deviceid = XIAllMasterDevices, .mask_len = MASK_LEN, .mask = (void*)&ximaskWin0 }};
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
	_short n, g, g1;
	double x,y;
	double tail;
} Touch;
Touch touch[TOUCH_MAX];
_short P=0,N=0;

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
#define DEF_SAFE (7+32+64)
#else
#define DEF_RR 0
#define DEF_SAFE (5+32+64)
#endif

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
	"touch device 0=auto",
	"min fingers",
	"max fingers",
	"button 2|3 hold time, ms",
	"min swipe x/y or y/x",
	"swipe size (>0 - dots, <0 - -mm)",
	"add to coordinates (to round to integer)",
	"	1=floating devices\n"
	"		0=master (visual artefacts, but enable native masters touch clients)\n"
	"		2=dynamic master/floating (firefox segfaults)",
	"RandR monitor name\n"
	"		or number 1+\n"
	"		'-' strict (only xinput prop 'xtg output' name)"
#ifdef USE_EVDEV
	"\n		or negative -<x> to find monitor, using evdev touch input size, grow +x\n"
#endif
	"			for (all|-d) absolute pointers (=xinput map-to-output)",
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
	"		12(+2048) try delete unused (saved) properity\n"
#ifdef _BACKLIGHT
	"		13(+4096) track backlight-controlled monitors and off when empty (under construction)\n"
#endif
	"		(auto-panning for openbox+tint2: \"xrandr --noprimary\" on early init && \"-s 135\" (7+128))",
	""
#ifdef XSS
	"fullscreen \"content type\" prop.: see \"xrandr --props\" (I want \"Cinema\")"
#endif
	""
#ifdef XSS
	"fullscreen \"Colorspace\" prop.: see \"xrandr --props\"  (I want \"DCI-P3_RGB_Theater\")"
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

char *_aret[5] = {NULL,NULL,NULL,NULL,NULL};
static char *natom(int i,Atom a){
	_free(_aret[i]);
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
static _short getGeometry(){
//	return XGetWindowAttributes(dpy, win, &wa);
	return XGetGeometry(dpy,win,&wa.root,&wa.x,&wa.y,&wa.width,&wa.height,&wa.border_width,&wa.depth);
}
static void monFullScreen(int x, int y, _short mode);
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
		if ((aCTFullScr||aCSFullScr) && getGeometry()) monFullScreen(wa.x,wa.y,1);
#endif
	}
}
#endif

#ifdef XTG
_short grabcnt = 0;
_int grabserial = -1;
static void _Xgrab(){
		grabserial=(grabserial+1)&0xffff;
		XGrabServer(dpy);
}
static void _Xungrab(){
		XUngrabServer(dpy);
//		grabserial=(grabserial+1)&0xffff;
}
static void _grabX(){
	if (!(grabcnt++)) _Xgrab();
}
static inline void forceUngrab(){
	if (grabcnt) {
		XFlush(dpy);
		_Xungrab();
		grabcnt=0;
	}
}
static inline void _ungrabX(){
	if ((pi[p_safe]&4) && !--grabcnt) {
		XFlush(dpy);
		_Xungrab();
	}
}

static void monFullScreen(int x, int y, _short mode);

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
	_short reason;
	unsigned int x,y,width,height;
} dinf_t;

#define o_master_ptr 1
#define o_floating 2
#define o_absolute 4
#define o_directTouch 8
///#define o_scroll 16

#define NINPUT_STEP 4
dinf_t *inputs = NULL, *inputs1, *dinf, *dinf1, *dinf_last = NULL;
_int ninput = 0, ninput1 = 0;
#define DINF(x) for(dinf=inputs; (dinf!=dinf_last); dinf++) if (x)
#define DINF_A(x) for(dinf=inputs; (dinf!=dinf_last); dinf++) if ((dinf->type&O_absolute) && )

typedef struct _pinf {
	_short en;
	long range[2];
	prop_int val,val0;
	Atom prop,save;
} pinf_t;

// width/height: real/mode
// width0/height0: -||- or first preferred (default/max) - safe bit 9(+256)
// width1/height1: -||- not rotated (for panning)
typedef struct _minf {
	_short type;
	unsigned int x,y,x2,y2,width,height,width0,height0,width1,height1;
	unsigned long mwidth,mheight;
	RROutput out;
	RRCrtc crt;
	Rotation rotation;
	_short r90;
	Atom name;
	_short connection;
	pinf_t bl;
	pinf_t non_desktop;
#ifdef _BACKLIGHT
	Window win;
	_short obscured,entered,geom_ch;
#endif
} minf_t;

#define o_out 1
#define o_non_desktop 2
#define o_active 4
#define o_crt 8
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

static void xrrSet(){
	if (!xrrr && xrr) xrrr = XRRGetScreenResources(dpy,root);
}

static void xrrFree(){
	if (xrrr) {
		XRRFreeScreenResources(xrrr);
		xrrr = NULL;
	}
}

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



#ifdef USE_EVDEV
static double _evsize(struct libevdev *dev, int c) {
	double r = 0;
	const struct input_absinfo *x = libevdev_get_abs_info(dev, c);
	if (x && x->resolution) r = (r + x->maximum - x->minimum)/x->resolution;
	return r;
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
		dinf->ABS[0] = _evsize(device,ABS_X);
		dinf->ABS[1] = _evsize(device,ABS_Y);
#ifdef ABS_Z
		dinf->ABS[2] = _evsize(device,ABS_Z);
#endif
		libevdev_free(device);
		xiSetProp(aABS,aFloat,&dinf->ABS,NdevABS,0);
	}
	close(fd);
ret:
	if (grabcnt && (pi[p_safe]&2)) _Xgrab();
}
#endif

float matrix[9] = {0,0,0,0,0,0,0,0,0};
static void map_to(){
	devid = dinf->devid;
	float x=minf->x,y=minf->y,w=minf->width,h=minf->height,dx=pf[p_touch_add],dy=pf[p_touch_add];
	_short m = 1;
	if (pa[p_touch_add] && pa[p_touch_add][0] == '+' && pa[p_touch_add][1] == 0) {
		if (minf->mwidth && dinf->ABS[0]!=0.) dx = (dinf->ABS[0] - minf->mwidth)/2;
		if (minf->mheight && dinf->ABS[1]!=0.) dy = (dinf->ABS[1] - minf->mheight)/2;
	}
	if (dx!=0 && minf->mwidth) {
		float b = (w/minf->mwidth)*dx;
		x-=b;
		w+=b*2;
	}
	if (dy!=0 && minf->mheight) {
		float b = (h/minf->mheight)*dy;
		y-=b;
		h+=b*2;
	}
	x/=minf0.width;
	y/=minf0.height;
	w/=minf0.width;
	h/=minf0.height;
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
//	float matrix[9] = {0,0,x,0,0,y,0,0,1};
	matrix[0] = matrix[1] = matrix[3] = matrix[4] = matrix[6] = matrix[7] = 0;
	matrix[2]=x;
	matrix[5]=y;
	matrix[8]=1;
	matrix[1-m]=w;
	matrix[3+m]=h;
	xiSetProp(aMatrix,aFloat,&matrix,9,4);
}

static void fixMonSize(minf_t *minf, double *dpmw, double *dpmh) {
	if (!minf->mheight) return;
	double _min, _max, m,  mh = minf->height / *dpmh;

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

#define ACTIVE(minf) (minf->connection == RR_Connected)
#define EXISTS(minf) (minf->type&(2|4))
#define _BUFSIZE 1048576

static void xrGetRangeProp(Atom prop, Atom save, pinf_t *d) {
	XRRPropertyInfo *pinf;
	d->en = 0;
	if (!xrGetProp(prop,XA_INTEGER,&d->val,1,0) || !(pinf = XRRQueryOutputProperty(dpy,minf->out,prop))) return;
	if (!pinf->range || pinf->num_values != 2) goto free;
	d->en = 1;
	d->prop = prop;
	d->save = save;
	d->range[0] = pinf->values[0];
	d->range[1] = pinf->values[1];
	d->val0 = d->val;
	if (!xrGetProp(save,XA_INTEGER,&d->val0,1,0)) {
		XRRConfigureOutputProperty(dpy,minf->out,save,False,1,2,pinf->values);
		xrSetProp(save,XA_INTEGER,&d->val0,1,0);
	}
free:
	XFree(pinf);
}

static void xrSetRangeProp(pinf_t *d, prop_int val) {
	if (!d->en) return;
	if (val == d->val) return;
	if (val < d->range[0]) val = d->range[0];
	if (val > d->range[1]) val = d->range[1];
	d->val = val;
	xrSetProp(d->prop,XA_INTEGER,&val,1,0);
}

#ifdef _BACKLIGHT
static void chBL1(_short obscured, _short entered) {
	if (obscured != 2) minf->obscured = obscured;
	if (entered != 2) minf->entered = entered;
	xrSetRangeProp(&minf->bl,(minf->obscured || minf->entered) ? minf->bl.save : minf->bl.range[0]);
}

static void chBL(Window w, _short obscured, _short entered) {
	MINF(minf->win == w && (minf->type&o_backlight)) {
//	MINF(minf->win == w) {
		chBL1(obscured,entered);
		break;
	}
}

#if 0
#define __check_entered
static void checkEntered(){
	DINF(o_master_ptr|o_floating) {
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
	if (win == root) return 0;
	if (minf && (win ? (minf->win == win) : (win = minf->win))) goto found;
	MINF(minf->win == win) goto found;
	return 0;
found:
	if (minf->x == x && minf->y == y && minf->width == w && minf->height == h) return 2;
	minf->geom_ch = 1;
	xrrFree();
	return 1;
}

XSetWindowAttributes winattr = {
	.override_redirect = True,
	.do_not_propagate_mask = ~(long)VisibilityChangeMask,
	.event_mask = VisibilityChangeMask
//		|EnterWindowMask|LeaveWindowMask|ButtonPressMask|ButtonReleaseMask
};

// bits: 1 - free if exists, 2 - low/high
static void simpleWin(_short mode) {
	Window w = minf->win;
	if ((mode&1)) {
		XDestroyWindow(dpy,w);
		w = 0;
	}
	if (!w) {
		w = XCreateSimpleWindow(dpy, root, minf->x,minf->y,minf->width,minf->height,0,0,BlackPixel(dpy, screen));
		XChangeWindowAttributes(dpy, w, CWOverrideRedirect|CWEventMask, &winattr);
		XISelectEvents(dpy, w, ximaskWin, Nximask);
	}
	if (mode&2) XRaiseWindow(dpy,w);
	else XLowerWindow(dpy, w);
//	if (!minf->win)
	    XMapWindow(dpy, w);
//	DBG("window %lx",w);
	minf->win = w;
}

// what: 0 - BL check, 1 - bl recreate, 2 - all UP
static void setFSWin() {
}

#endif

RROutput prim0, prim;

static void xrMons0(){
    unsigned long i;
    xrrFree();
    _grabX();
//    XFlush(dpy);
//    XSync(dpy,False);
    xrrSet();
    int nout = -1, active = 0, non_desktop = 0, non_desktop0 = 0, non_desktop1 = 0;
    XRRCrtcInfo *cinf = NULL;
    XRROutputInfo *oinf = NULL;
    i = xrrr ? xrrr->noutput : 0;
    if (noutput != i) {
	if (outputs) {
#ifdef _BACKLIGHT
		MINF(minf->win) XDestroyWindow(dpy,minf->win);
#endif
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
	oldShowPtr |= 2;
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
		minf->non_desktop.en = 0;
		minf->bl.en = 0;
		minf->type = 0;
		if (!(minf->out = xrrr->outputs[nout])) continue;
		minf->type |= o_out;
		if (minf->out == prim) minf->type |= o_primary;
		if (oinf = XRRGetOutputInfo(dpy, xrrr, minf->out)) break;
	}
	if (!oinf) break;
	if ((minf->connection = oinf->connection) == RR_Connected) {
		minf->type |= o_active;
		active++;
	}
	xrGetRangeProp(aNonDesk,aNonDeskSave,&minf->non_desktop);
	if (minf->non_desktop.en) {
		if (minf->non_desktop.val) {
			minf->type |= o_non_desktop;
			non_desktop++;
		}
		if (minf->non_desktop.val0) {
			non_desktop0++;
			if ((minf->type&o_active)) active--; // think there are not active-born
			if (!minf->non_desktop.val) non_desktop1++;
		}
	}
	if ((minf->crt = oinf->crtc)) minf->type |= o_crt;
	minf->name = XInternAtom(dpy, oinf->name, False);
	minf->mwidth = oinf->mm_width;
	minf->mheight = oinf->mm_height;
	if (minf->mwidth && minf->mheight) minf->type |= o_msize;
	xrGetRangeProp(aBl,aBlSave,&minf->bl);
#ifdef _BACKLIGHT
	if (minf->bl.en) minf->type |= o_backlight;
#else
	// repair
	if (minf->bl.en && minf->bl.val0 != minf->bl.val) xrSetRangeProp(&minf->bl,minf->bl.val0);
#endif
	// looks like Xorg want: 1) crtc 2) width & height from crtc 3) not non-desktop. So, prescan all potential.
	if ((minf->type&(1|4|8))!=(1|4|8) || !(cinf=XRRGetCrtcInfo(dpy, xrrr, minf->crt)))
		goto next;
	minf->width = minf->width0 = cinf->width;
	minf->height = minf->height0 = cinf->height;
	if (minf->x != cinf->x || minf->y != cinf->y) {
		minf->x = cinf->x;
		minf->y = cinf->y;
		minf->geom_ch = 1;
	}
	minf->rotation = cinf->rotation;
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
	if ((minf->r90 = !!(minf->rotation & (RR_Rotate_90|RR_Rotate_180)))) {
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
		minf->geom_ch = 1;
	}
	if (minf->width && minf->height) {
		minf->type |= o_size;
	}
next:
	if (minf->geom_ch) {
		oldShowPtr |= 2;
		minf->geom_ch = 0;
#ifdef _BACKLIGHT
		if (minf->win) {
			XDestroyWindow(dpy,minf->win);
			minf->win = 0;
		}
#endif
	}
    }
    if (!active && non_desktop)
	MINF(minf->non_desktop.en && minf->non_desktop.val)
	    xrSetRangeProp(&minf->non_desktop,0);
    if (active && non_desktop1)
	MINF(minf->non_desktop.en && minf->non_desktop.val)
	    xrSetRangeProp(&minf->non_desktop,minf->non_desktop.val0);
#ifdef _BACKLIGHT
    if ((pi[p_safe]&4096)) {
	MINF_T2(o_active|o_backlight,o_active|o_backlight) {
		if (!minf->win) simpleWin(0);
    } else if (minf->win) {
		XDestroyWindow(dpy,minf->win);
		minf->win = 0;
		oldShowPtr |= 2;
    }}
#endif
ret: {}
//    _ungrabX(); // forceUngrab() instead
}

unsigned int pan_x,pan_y,pan_cnt;
static void _pan(minf_t *m) {
	if (pi[p_safe]&64 || !m || !m->crt) return;
	xrrSet();
	if (!xrrr) return;
	XRRPanning *p = XRRGetPanning(dpy,xrrr,m->crt);
	if (!p) return;
	if (p->top != pan_y || p->left || p->width != m->width1 || p->height != m->height1) {
		p->top = pan_y;
		p->left = 0;
		p->width = m->width1;
		p->height = m->height1;
		DBG("crtc %lu panning %ix%i+%i+%i/track:%ix%i+%i+%i/border:%i/%i/%i/%i",m->crt,p->width,p->height,p->left,p->top,  p->track_width,p->track_height,p->track_left,p->track_top,  p->border_left,p->border_top,p->border_right,p->border_bottom);
		if (XRRSetPanning(dpy,xrrr,m->crt,p)==Success) pan_cnt++;
	}
	pan_y = _max(pan_y,p->top + m->height0 + p->border_top + p->border_bottom);
	pan_x = _max(pan_x,p->left + m->width0 + p->border_left + p->border_right);
	XRRFreePanning(p);
}

#ifdef XSS
static void _monFS(Atom prop,Atom save,Atom val,int x, int y, _short mode){
#if 1
		// optimized X calls, but stricter (?) off/on states
		if (!val && mode) return;
		Atom ct1 = 0;
		if (!noXSS) {
saved:
			if(xrGetProp(save,XA_ATOM,&ct1,1,0) && ct1) {
				_short del = !val || !!(pi[p_safe]&2048) || !mode;
				if (xrSetProp(prop,XA_ATOM,&ct1,1,4|(del<<4)) && del)
					XRRDeleteOutputProperty(dpy,minf->out,save);
			} 
			return;
		}
		if (!val) return;
		// repair disconnected
		if (!ACTIVE(minf)) goto saved;
		// repair unordered
		if (!(x>=minf->x && x<=minf->x2 && y>=minf->y && y<=minf->y2)) goto saved;
		Atom ct = 0;
		if (!xrGetProp(prop,XA_ATOM,&ct,1,0) || !ct) return;
		XRRPropertyInfo *pinf = NULL;
		if (!xrGetProp(save,XA_ATOM,&ct1,1,0) || !ct1) {
reconf:
			if (!(pinf = XRRQueryOutputProperty(dpy,minf->out,prop))) return;
			int i;
			for (i=0; i<pinf->num_values; i++) if (pinf->values[i] == val) goto ok;
			XFree(pinf);
			return;
ok:
			XRRConfigureOutputProperty(dpy,minf->out,save,False,pinf->range,pinf->num_values,pinf->values);
			XFree(pinf);
		}
		if (ct != ct1) {
			if (!xrSetProp(save,XA_ATOM,&ct,1,0x10)) {
				if (!pinf) {
					XRRDeleteOutputProperty(dpy,minf->out,save);
					goto reconf;
				}
				return;
			}
		}
		if (ct != val)
			xrSetProp(prop,XA_ATOM,&val,1,0);
#else
		// simple, slow
		Atom ct = 0;
		if (!xrGetProp(prop,XA_ATOM,&ct,1,0) || !ct) return;
		Atom ct1 = 0;
		xrGetProp(save,XA_ATOM,&ct1,1,0);
		XRRPropertyInfo *pinf = NULL;
		if (val && ACTIVE(minf) && noXSS && x>=minf->x && x<=minf->x2 && y>=minf->y && y<=minf->y2
		    && (pinf = XRRQueryOutputProperty(dpy,minf->out,prop))
		    && !pinf->range) {
			int i;
			for (i=0; i<pinf->num_values; i++) {
				if (pinf->values[i]==val) {
					if (ct != ct1) {
						XRRDeleteOutputProperty(dpy,minf->out,save);
						//if (!ct1)
						    XRRConfigureOutputProperty(dpy,minf->out,save,False,pinf->range,pinf->num_values,pinf->values);
						xrSetProp(save,XA_ATOM,&ct,1,0);
					}
					ct1 = val;
					break;
				}
			}
		}
		if (pinf) XFree(pinf);
		if (ct1 && ct != ct1) {
			DBG("output: %s %s: %s -> %s",minf?natom(0,minf->name):"",natom(1,prop),natom(2,ct),natom(3,ct1));
			xrSetProp(prop,XA_ATOM,&ct1,1,0);
		}
#endif
}

static void monFullScreen(int x, int y, _short mode) {
	xrrSet(); // flush
	MINF_T(o_out) {
		_monFS(aCType,aCType1,aCTFullScr,x,y,mode);
		_monFS(aColorspace,aColorspace1,aCSFullScr,x,y,mode);
	}

}

static int scrWin() {
	minf = &minf0;
	simpleWin(3);
}
#endif

static void fixGeometry(){
	_int nm = 0;
	minf2 = minf1 = NULL;
	if (!xrr) return;
	if (resDev) {
		DINF(dinf->devid == resDev) {
			MINF(dinf->mon == minf->name) {
				minf2 = minf;
				goto find1;
			}
			break;
		}
		if (devid == resDev) return;
	}
	DINF((dinf->type&o_directTouch)) {
		MINF(minf->type&o_active && dinf->mon == minf->name) {
			minf2 = minf;
			goto find1;
		}
	}
find1:
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
	if (!xrrr) {
		//xrMons0();
		xrrSet(); 
	}
//	fixMMRotation(minf2);
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

		if (!pan_x)
			pan_x = minf0.width;
		if (!pan_y)
			pan_y = minf0.height;
		if (pan_cnt && (pan_x != minf0.width || pan_y != minf0.height)) {
			XFlush(dpy);
			XSync(dpy,False);
		} //else if (!do_dpi) goto noxrr1;

		if (do_dpi) {
			if (minf2->crt && minf1->crt != minf2->crt && minf2->crt) fixMonSize(minf2, &dpmw, &dpmh);
			fixMonSize(minf1, &dpmw, &dpmh);
		}

		unsigned int mh, mw;
rep_size:
		if (do_dpi) {
			if (!(pi[p_safe]&512))
				if (dpmh > dpmw) dpmw = dpmh;
				else dpmh = dpmw;
			mh = pan_y / dpmh + .5;
			mw = pan_x / dpmw + .5;
		} else {
			// keep dpi after panning
			mh = (pan_y * minf0.mheight + .0) / minf0.height + .5;
			mw = (pan_x * minf0.mwidth + .0) / minf0.width + .5;
		}

		if (
		    pan_cnt ||	// force RRScreenChangeNotify event after panning for pre-RandR 1.2 clients
		    mw != minf0.mwidth || mh != minf0.mheight || pan_y != minf0.height || pan_x != minf0.width) {
			DBG("dpi %.1fx%.1f -> %.1fx%.1f dots/mm %ux%u/%lux%lu -> %ux%u/%ux%u",25.4*minf0.width/minf0.mwidth,25.4*minf0.height/minf0.mheight,25.4*pan_x/mw,25.4*pan_y/mh,minf0.width,minf0.height,minf0.mwidth,minf0.mheight,pan_x,pan_y,mw,mh);
			_error = 0;
			XRRSetScreenSize(dpy, root, pan_x, pan_y, mw, mh);
			if (pan_x != minf0.width || pan_y != minf0.height) {
				XFlush(dpy);
				XSync(dpy,False);
				if (_error) {
					pan_x = minf0.width;
					pan_y = minf0.height;
					goto rep_size;
				}
			}
		}
	}
ex:
	xrrFree();
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
_short __init = 1;


_int ninput1_;
// remember only devices on demand
static void _add_input(_short t){
	if (dinf1) goto ex;
	_int i = ninput1_++;
	if (tdevs2 > ninput1 || !inputs1) {
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
ex:
	dinf1->type |= t;
}

static void _reason(char *s){
	DBG("input %i == monitor %s: %s",dinf->devid,dinf->mon?natom(0,dinf->mon):"none",s);
}

static void touchToMon(){
	unsigned long xy[2];
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
#define _REASON(m,r,txt) dinf->reason=r; if (dinf->mon!=m) {dinf->mon=m;_reason(txt);} continue;
	DINF((dinf->type&o_absolute) && (dinf->reason || !dinf->mon)) {
		Atom m = 0;
		if (mon) {
			m = mon;
			_REASON(m,0,"by command line");
		} else {
			xiGetProp(aDevMon,XA_ATOM,&m,1,0);
			if (m) {
				_REASON(m,0,"by xinput property");
			}
		}
		if (m) continue;
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
		if (n == 1) {
			_REASON(m,1,"by size");
		}
		if (dinf->mon && dinf->reason<2) continue;
		// unprecise methods now
		if (m0) {
			_REASON(m0,3,"\"zero mm size\" monitor by proportions first");
		}
		if (m) {
			_REASON(m,3,"by size first");
		}
	}
	DINF(dinf->mon) MINF(minf->name == dinf->mon) {
		xy[0]=dinf->ABS[0];
		xy[1]=dinf->ABS[1];
		if (!xy[0] || !xy[1]) {
			if (minf->type&o_msize) {
				_reason("input size from monitor");
				xy[minf->r90] = minf->mwidth;
				xy[!minf->r90] = minf->mheight;
			} else break;
		} else if (!(minf->type&o_msize)) {
			_reason("monitor size from device (broken video driver or monitor)");
			minf->mwidth = xy[minf->r90];
			minf->mheight = xy[minf->r90];
		}
//		if (!(minf->type&o_active)) break
		if (dinf->x != minf->x || dinf->y != minf->y ||
			    dinf->width != minf->width || dinf->height != minf->height) {
				dinf->x = minf->x;
				dinf->y = minf->y;
				dinf->width = minf->width;
				dinf->height = minf->height;
				map_to();
		}
		break;
	}
}

static void getHierarchy(){
	static XIAttachSlaveInfo ca = {.type=XIAttachSlave};
	static XIDetachSlaveInfo cf = {.type=XIDetachSlave};
	static XIAddMasterInfo cm = {.type = XIAddMaster, .name = "TouchScreen", .send_core = 0, .enable = 1};
	static XIRemoveMasterInfo cr = {.type = XIRemoveMaster, .return_mode = XIFloating};

	inputs1 = NULL;
	ninput1_ = 0;
	
	int i,j,ndevs2,nkbd,m=0;
	short grabbed = 0, ev2=0;

	if ((pi[p_safe]&7)==5) {_grabX();grabbed=1;} // just balance grab count
	int _grab = grabserial;
	XIDeviceInfo *info2 = XIQueryDevice(dpy, XIAllDevices, &ndevs2);
	if (pi[p_floating] != 1) {
	    for (i=0; i<ndevs2; i++) {
		XIDeviceInfo *d2 = &info2[i];
		devid = d2->deviceid;
		switch (d2->use) {
		    case XIMasterPointer:
			if (!cr.return_pointer) cr.return_pointer = devid;
			else if (cr.return_pointer != devid && !strncmp(d2->name,cm.name,11)) {
				m = devid;
				if (ca.new_master != m) {
					ximask[0].mask=(void*)&ximaskTouch;
					ximask[0].deviceid=m;
					XISelectEvents(dpy, root, ximask, Nximask);
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
		XIDeviceInfo *d2 = &info2[i];
		devid = d2->deviceid;
		dinf1 = NULL;
		_short t = 0, scroll = 0;
		_short type = 0;
		busy = 0;
		if (__init) {
			XIDeleteProperty(dpy,devid,aABS);
		}
		switch (d2->use) {
		    case XIMasterPointer:
#ifdef __check_entered
			type|=o_master_ptr;
#endif
			break;
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
//			break;
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
				type|=o_directTouch;
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
					if (e->number >= 0 && e->number <= NdevABS) {
						dinf1->xABS[e->number].min = e->min;
						dinf1->xABS[e->number].max = e->max;
						dinf1->xABS[e->number].resolution = e->resolution;
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
		if (pa[p_device] && *pa[p_device]) {
			if (!strcmp(pa[p_device],d2->name) || pi[p_device] == devid) {
				t = 1;
				scroll = 0;
				type|=o_directTouch;
			} else {
				scroll = 1;
				//type|=o_scroll;
				type &= ~(_short)o_directTouch;
				//t = 0;
				goto skip_map;
			}
		}
skip_map:
		tdevs+=t;
		// exclude touchscreen with scrolling
		void *c = NULL;
		cf.deviceid = ca.deviceid = ximask[0].deviceid = devid;
		ximask[0].mask = NULL;
		switch (d2->use) {
		    case XIFloatingSlave:
//			if (!t && !scroll) continue;
			if (!t || scroll) break;
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
					ximask[0].mask=(void*)&ximask0;
				} else {
					ximask[0].mask=(void*)&ximaskTouch;
				}
				break;
			}
			break;
		    case XISlavePointer:
			if (!t) ximask[0].mask = (void*)(showPtr ? &ximask0 : &ximaskButton);
			else if (scroll) ximask[0].mask = (void*)(showPtr ? &ximaskTouch : &ximask0);
			else {
				tdevs2++;
				switch (pi[p_floating]) {
				    case 1:
					ximask[0].mask=(void*)&ximaskTouch;
					XIGrabDevice(dpy,devid,root,0,None,XIGrabModeSync,XIGrabModeTouch,False,ximask);
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
				oldShowPtr |= 4;
				//fprintf(stderr,"busy delay\n");
				continue;
			}
			for (j=P; j!=N; j=TOUCH_N(j)) if (touch[j].deviceid == devid) goto busy;

			if (!grabbed) {
				grabbed = 1;
				_grabX();
			}

			if (_grab != grabserial) {
				// server was relocked (getEvRes...), reread device state
				d2 = XIQueryDevice(dpy, devid, &j);
				if (!d2 || j < 1 || d2->deviceid != devid) {
					if (d2) XIFreeDeviceInfo(d2);
					d2 = &info2[i];
				}
			}
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
			break;
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
	if (pi[p_safe]&8) XFixesShowCursor(dpy,root);
	tdevs -= tdevs2;

	if (inputs) free(inputs);
	inputs = inputs1;
	ninput = ninput1 = ninput1_;
	dinf_last = &inputs[ninput];


	touchToMon();
	devid = 0;
	fixGeometry();
	if (grabbed) _ungrabX();
#ifdef XSS
	if (__init)
		monFullScreen(0,0,0);
#endif

	__init = 0;
}

static void setShowCursor(){
	switch (oldShowPtr&0xf8) {
	    case 0: goto x0;
	    case 8:
		xrMons0();
		break;
	}
	if ((oldShowPtr&7)==showPtr) {
		oldShowPtr = showPtr;
		return;
	}
x0:
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
	minf0.width = DisplayWidth(dpy,screen);
	minf0.height = DisplayHeight(dpy,screen);
	minf0.mwidth = DisplayWidthMM(dpy,screen);
	minf0.mheight = DisplayHeightMM(dpy,screen);
	fixMMRotation(&minf0);
	oldShowPtr |= 2;
}
#endif

static void getWinGrp(){
	if (win1==None) {
		XGetInputFocus(dpy, &win1, &revert);
		if (win1==None) win1 = root;
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
	getWProp(win,aWMState,XA_ATOM,sizeof(Atom));
	WMState(((Atom*)ret),n);
#endif
}

static void setWinGrp(){
	printGrp();
	if (win!=root)
		XChangeProperty(dpy,win,aKbdGrp,XA_CARDINAL,32,PropModeReplace,(unsigned char*) &grp1,1);
	grp = grp1;
}

static void getPropWin1(){
	if (getWProp(root,aActWin,XA_WINDOW,sizeof(Window)))
		win1 = *(Window*)ret;
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
	    case BadAccess: // second XI_Touch* root listener
		oldShowPtr |= 2;
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
	aNonDesk = XInternAtom(dpy, RR_PROPERTY_NON_DESKTOP, False);
	aNonDeskSave = XInternAtom(dpy, "xtg saved " RR_PROPERTY_NON_DESKTOP, False);
	aBl = XInternAtom(dpy, RR_PROPERTY_BACKLIGHT, False);
	aBlSave = XInternAtom(dpy, "xtg saved " RR_PROPERTY_BACKLIGHT, False);
#ifdef _BACKLIGHT
	XISetMask(ximaskWin0, XI_Leave);
	XISetMask(ximaskWin0, XI_Enter);
	XISetMask(ximaskWin0, XI_ButtonPress);
	XISetMask(ximaskWin0, XI_ButtonRelease);
#endif
#ifdef USE_EVDEV
	aNode = XInternAtom(dpy, "Device Node", False);
	aABS = XInternAtom(dpy, "Device libevdev ABS", False);
#else
	aABS = XInternAtom(dpy, "xtg ABS", False);
#endif
#ifdef XSS
	aCType = XInternAtom(dpy, "content type", False);
	aCType1 = XInternAtom(dpy, "xtg saved content type", False);
	aColorspace = XInternAtom(dpy, "Colorspace", False);
	aColorspace1 = XInternAtom(dpy, "xtg saved Colorspace", False);
	if (pa[p_content_type]) aCTFullScr = XInternAtom(dpy, pa[p_content_type], False);
	if (pa[p_colorspace]) aCSFullScr = XInternAtom(dpy, pa[p_colorspace], False);
#endif

	XISetMask(ximaskButton, XI_ButtonPress);
	XISetMask(ximaskButton, XI_ButtonRelease);
	XISetMask(ximaskButton, XI_Motion);

	XISetMask(ximaskTouch, XI_TouchBegin);
	XISetMask(ximaskTouch, XI_TouchUpdate);
	XISetMask(ximaskTouch, XI_TouchEnd);

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
#ifdef RRCrtcChangeNotifyMask
			xrevent1 = xrevent + RRNotify;
#endif
			xrevent += RRScreenChangeNotify;
			XRRSelectInput(dpy, root, RRScreenChangeNotifyMask
#ifdef RRCrtcChangeNotifyMask
				|RRCrtcChangeNotifyMask
				|RROutputChangeNotifyMask
				|RROutputPropertyNotifyMask
#endif
				);
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
	curShow = 0;
	showPtr = 1;
	oldShowPtr |= 8;
	XFixesShowCursor(dpy,root);
#endif
	win = root;
}

int main(int argc, char **argv){
#ifdef XTG
	_short i;
	int opt;
	Touch *to;
	double x1,y1,x2,y2,xx,yy,res;
	_short end,tt,g,bx,by;
	_int k;
	TouchTree *m;

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
#ifdef _BACKLIGHT
		if (!xrrr) xrMons0();
#endif
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
				if (ev.xcookie.evtype < XI_TouchBegin) switch (ev.xcookie.evtype) {
#ifdef DEV_CHANGED
				    case XI_DeviceChanged:
#undef e
#define e ((XIDeviceChangedEvent*)ev.xcookie.data)
					if (XGetEventData(dpy, &ev.xcookie)) {
						TIME(T,e->time);
						int r = e->reason;
						fprintf(stderr,"dev changed %i %i (%i)\n",r,e->deviceid,devid);
						XFreeEventData(dpy, &ev.xcookie);
						if (r == XISlaveSwitch) goto ev2;
					}
					oldShowPtr |= 2;
					continue;
#endif
#undef e
#define e ((XIPropertyEvent*)ev.xcookie.data)
				    case XI_PropertyEvent:
					if (XGetEventData(dpy, &ev.xcookie)) {
						TIME(T,e->time);
						Atom p = e->property;
						devid = e->deviceid;
						XFreeEventData(dpy, &ev.xcookie);
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
				    case XI_HierarchyChanged:
					oldShowPtr |= 2;
					continue;
#ifdef _BACKLIGHT
#undef e
#define e ((XIEnterEvent*)ev.xcookie.data)
				    case XI_Leave:
				    case XI_Enter:
					if (XGetEventData(dpy, &ev.xcookie)) {
						_short st = ev.xcookie.evtype==XI_Enter;
						if (e->event_x >= 0 && e->event_y >= 0) {
							MINF(e->event == minf->win && minf->bl.en
							    && e->event_x < minf->width && e->event_y < minf->height) {
								st = 1;
								break;
							}
						}
						chBL(e->event,2,st);
						XFreeEventData(dpy, &ev.xcookie);
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
				if (!tdevs2 || !XGetEventData(dpy, &ev.xcookie)) goto ev;
				showPtr = 0;
#undef e
#define e ((XIDeviceEvent*)ev.xcookie.data)
//				fprintf(stderr,"ev2 %i %i\n",e->deviceid,ev.xcookie.evtype);
				TIME(T,e->time);
				res = resX;
				tt = 0;
				m = &bmap;
				devid = e->deviceid;
				end = (ev.xcookie.evtype == XI_TouchEnd
						|| (e->flags & XITouchPendingEnd));
				x2 = e->root_x + pf[p_round], y2 = e->root_y + pf[p_round];
#if 1
				if (resDev != devid) {
					resDev = devid;
					// force swap monitors if dynamic pan?
					fixGeometry();
				}
#endif
				g = ((int)x2 <= minf2->x) ? BUTTON_RIGHT : ((int)x2 >= minf2->x2) ? BUTTON_LEFT : ((int)y2 <= minf2->y) ? BUTTON_UP : ((int)y2 >= minf2->y2) ? BUTTON_DOWN : 0;
				if (g) tt |= PH_BORDER;
				for(i=P; i!=N; i=TOUCH_N(i)){
					Touch *t1 = &touch[i];
					if (t1->touchid != e->detail || t1->deviceid != devid) continue;
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
					if (end) goto evfree;
					to = &touch[N];
					N=TOUCH_N(N);
					if (N==P) P=TOUCH_N(P);
					to->touchid = e->detail;
					to->deviceid = devid;
					XFreeEventData(dpy, &ev.xcookie);
					TIME(to->time,T);
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
				XFreeEventData(dpy, &ev.xcookie);
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
			XFreeEventData(dpy, &ev.xcookie);
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
#endif
#undef e
#define e (ev.xconfigure)
		    case ConfigureNotify:
			if (fixWin(e.window,e.x,e.y,e.width,e.height) || !xrr || !XRRUpdateConfiguration(&ev)) goto ev;
			_scr_size();
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
				xrrFree();
				oldShowPtr |= 8;
				//minf0.rotation = e->rotation;
				break;
			}
#ifdef RRCrtcChangeNotifyMask
#undef e
#define e ((XRRNotifyEvent*)&ev)
			if (ev.type == xrevent1) {
				// RANDR time(s) is static
				//XRRTimes(dpy,screen,&T);
				switch (e->subtype) {
#undef e
#define e ((XRROutputPropertyNotifyEvent*)&ev)
				    case RRNotify_OutputProperty:
					// required reinit XRRGetScreenResources(dpy,root) to flush
					xrrFree();
					if (e->property != aNonDesk) break;
#undef e
#define e ((XRRNotifyEvent*)&ev)
				    default:
					xrrFree();
					oldShowPtr |= 8;
					break;
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
}
