/*
	xtg v1.35 - per-window keyboard layout switcher [+ XSS suspend].
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
#undef _BACKLIGHT
#define PROP_FMT
#undef PROP_FMT
#endif

// fixme: no physical size, only map-to-output on unknown case
#undef USE_XINERAMA

#include <stdio.h>
#include <stdint.h>

#include <X11/Xproto.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>

#ifdef XSS
#include <X11/extensions/scrnsaver.h>
#endif

#ifdef XTG
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/randr.h>
#ifdef USE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
//#include <X11/extensions/XInput.h>
#ifdef USE_EVDEV
#include <libevdev/libevdev.h>
#endif
#include <X11/extensions/XInput2.h>

#include <X11/extensions/XTest.h>
#include <X11/extensions/Xfixes.h>
#include <stdlib.h>
#include <string.h>
//#include <sys/time.h>
#include <X11/Xpoll.h>
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
Window win, win1;
int screen;
Window root;
Atom aActWin, aKbdGrp, aXkbRules;
Bool _error;
#ifdef XSS
Atom aWMState, aFullScreen;
Bool noXSS, noXSS1;
#endif
CARD32 grp, grp1;
unsigned char *rul, *rul2;
unsigned long rulLen, n;
int xkbEventType, xkbError, n1, revert;
XEvent ev;
unsigned char *ret;

static inline long min(long x,long y){ return x<y?x:y; }
static inline long max(long x,long y){ return x>y?x:y; }

#ifdef XTG
//#define DEV_CHANGED
int devid = 0;
int xiopcode, xierror, xfopcode=0, xfevent=0, xferror=0;
Atom aFloat,aMatrix,aABS,aDevMon,aDevMonCache;
#ifdef _BACKLIGHT
Atom aBl;
#endif
int floatFmt = sizeof(float) << 3;
int atomFmt = sizeof(Atom) << 3;
#define prop_int int32_t
int intFmt = sizeof(prop_int) << 3;
#ifdef USE_EVDEV
Atom aNode;
#endif
#ifdef XSS
Atom aCType, aCType1, aCTFullScr = 0;
#endif

#define MASK_LEN XIMaskLen(XI_LASTEVENT)
typedef unsigned char xiMask[MASK_LEN];
xiMask ximaskButton = {}, ximaskTouch = {}, ximask0 = {};
XIEventMask ximask = { .deviceid = XIAllDevices, .mask_len = MASK_LEN, .mask = (void*)&ximask0 };
//#define MASK(m) ((void*)ximask.mask=m)

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
int resDev;
int xrr, xrevent, xrerror;
Atom aMonName = None;
#ifdef USE_XINERAMA
int xir, xirevent, xirerror;
#endif

int xssevent, xsserror;
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
#define MAX_PAR 16
char *pa[MAX_PAR] = {};
// android: 125ms tap, 500ms long
#define PAR_DEFS { 0, 1, 2, 1000, 2, -2, 0, 1, DEF_RR, 0,  14, 32, 72, 0, DEF_SAFE, 0 }
int pi[] = PAR_DEFS;
double pf[] = PAR_DEFS;
char pc[] = "d:m:M:t:x:r:e:f:R:a:o:O:p:P:s:c:h";
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
	"		(auto-panning for openbox+tint2: \"xrandr --noprimary\" on early init && \"-s 135\" (7+128))",
	""
#ifdef XSS
	"fullscreen content type: see \"xrandr --props\" -> \"content type\" (I want \"Cinema\")"
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
	int xievent = 0, xi = 0;
	xiopcode = 0;
	XQueryExtension(dpy, "XInputExtension", &xiopcode, &xievent, &xierror);
#endif
}

Atom pr_t;
int pr_f;
unsigned long pr_b,pr_n;


#define _free(p) if (p) { XFree(p); p=NULL; }

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

static int getProp(Window w, Atom prop, Atom type, int size){
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
#ifdef XTG
static void monFullScreen(int x, int y);
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
		XScreenSaverSuspend(dpy,noXSS=noXSS1);
#ifdef XTG
		if (aCTFullScr) {
			XWindowAttributes wa;
			XGetWindowAttributes(dpy, win, &wa);
			monFullScreen(wa.x,wa.y);
		}
#endif
	}
}
#endif

#ifdef XTG

typedef struct _pinf {
	_short en;
	long range[2];
	prop_int val;
} pinf_t;

// width/height: real/mode
// width0/height0: -||- or first preferred (default/max) - safe bit 9(+256)
// width1/height1: -||- not rotated (for panning)
typedef struct _minf {
	unsigned int x,y,x2,y2,width,height,width0,height0,width1,height1;
	unsigned long mwidth,mheight;
	RROutput out;
	RRCrtc crt;
	Rotation rotation;
	Atom name;
#ifdef _BACKLIGHT
	pinf_t bl;
#endif
} minf_t;

minf_t minf,minf1,minf2;

int scr_width, scr_height, scr_mwidth, scr_mheight;
//int scr_rotation;
// usual dpi calculated from height only"
_short resXY,mon_sz,strict;
Atom mon = 0;
double mon_grow;

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
		)?8:(type==aFloat)?floatFmt:(type==XA_ATOM)?atomFmt:(type==XA_INTEGER)?intFmt:def;
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

	if (!to) {
		to = ret = malloc((toF>>3)*cnt);
	}
	for (i=0; i<cnt; i++) {
		if (fromT == XA_INTEGER) {
			switch (fromF) {
			case 64: x = ((int64_t*)from)[i]; break;
			case 32: x = ((int32_t*)from)[i]; break;
			case 16: x = ((int16_t*)from)[i]; break;
			case 8: x = ((int16_t*)from)[i]; break;
			}
		} else {
			switch (fromF) {
			case 64: x = ((uint64_t*)from)[i]; break;
			case 32: x = ((uint32_t*)from)[i]; break;
			case 16: x = ((uint16_t*)from)[i]; break;
			case 8: x = ((uint16_t*)from)[i]; break;
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
	if (ret != ret1) XFree(ret1);
	return to;
}
#endif

// chk return 2=eq +:
// 1 - read+cmp, 1=ok
// 2 - - (to check event or xiSetProp() force)
// 3 - 1=incompatible (xiSetProp() strict)
// 4 - 1=incompatible or none (xiSetProp() strict if exists)

static _short _chk_prop(char *pr, unsigned long who, Atom prop, Atom type, unsigned char **data, int cnt, _short chk){
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
			ERR("incompatible %s() result of %lu %s %s %i",pr,who,XGetAtomName(dpy,prop),XGetAtomName(dpy,pr_t),pr_f);
			return (chk>2);
		}
	}
	if (*data) {
		pr_n*=(f>>3);
		if (pr_t == XA_STRING) n++;
		if (chk && !memcmp(*data,ret,pr_n)) return 2;
		else if (chk<2) memcpy(*data,ret,pr_n);
		else return 0;
	} else *data = ret;
	return 1;
}

static _short xrGetProp(RROutput out, Atom prop, Atom type, void *data, int cnt, _short chk){
	_free(ret);
	Bool pending = False;
	XRRGetOutputProperty(dpy,out,prop,0,abs(cnt),False,pending,type,&pr_t,&pr_f,&pr_n,&pr_b,(void*)&ret);
	return _chk_prop("XRRGetOutputProperty",out,prop,type,(void*)&data,cnt,chk);
}

static void xrSetProp(RROutput out, Atom prop, Atom type, void *data, int cnt, _short chk){
	int f = fmtOf(type,32);
	if (chk) {
		if (xrGetProp(out,prop,type,data,cnt,chk)) return;
#ifdef PROP_FMT
		data = fmt2fmt(data,type,f,NULL,pr_t,pr_f,cnt);
		type = pr_t;
		f = pr_f;
#endif
	}
	if (data) XRRChangeOutputProperty(dpy,out,prop,type,f,PropModeReplace,data,cnt);
}

#ifdef _BACKLIGHT
static void xrChkProp(RROutput out, Atom prop, pinf_t *d) {
	XRRPropertyInfo *pinf;
	if (xrGetProp(out,prop,XA_INTEGER,&d->val,1,0) && (pinf = XRRQueryOutputProperty(dpy,out,prop))) {
		if (pinf->range && pinf->num_values == 2) {
			d->en = 1;
			d->range[0] = pinf->values[0];
			d->range[1] = pinf->values[1];
		}
		XFree(pinf);
	}
}
#endif

#ifdef ABS_Z
#define NdevABS 3
#else
#define NdevABS 2
#endif
float devABS[NdevABS] = {0,0,};

static _short xiGetProp(int devid, Atom prop, Atom type, void *data, int cnt, _short chk){
	_free(ret);
	XIGetProperty(dpy,devid,prop,0,abs(cnt),False,type,&pr_t,&pr_f,&pr_n,&pr_b,(void*)&ret);
	return _chk_prop("XIGetOutputProperty",devid,prop,type,(void*)&data,cnt,chk);
}

static void xiSetProp(int devid, Atom prop, Atom type, void *data, int cnt, _short chk){
	int f = fmtOf(type,32);
	if (chk) {
		if (xiGetProp(devid,prop,type,data,cnt,chk)) return;
#ifdef PROP_FMT
		data = fmt2fmt(data,type,f,NULL,pr_t,pr_f,cnt);
		type = pr_t;
		f = pr_f;
#endif
	}
	if (data) XIChangeProperty(dpy,devid,prop,type,f,PropModeReplace,data,cnt);
}

#ifdef USE_EVDEV
static double _evsize(struct libevdev *dev, int c) {
	double r = 0;
	const struct input_absinfo *x = libevdev_get_abs_info(dev, c);
	if (x && x->resolution) r = (r + x->maximum - x->minimum)/x->resolution;
	return r;
}
static void getEvRes(){
	if (!(pi[p_safe]&1024) && xiGetProp(devid,aABS,aFloat,&devABS,NdevABS,0)) return;
	memset(&devABS,0,sizeof(devABS));
	if (!xiGetProp(devid,aNode,XA_STRING,NULL,-1024,0)) return;
	if (grabcnt && (pi[p_safe]&2)) _Xungrab();
	int fd = open(ret, O_RDONLY|O_NONBLOCK);
	if (fd < 0) goto ret;
	struct libevdev *device;
	if (!libevdev_new_from_fd(fd, &device)){
		devABS[0] = _evsize(device,ABS_X);
		devABS[1] = _evsize(device,ABS_Y);
#ifdef ABS_Z
		devABS[2] = _evsize(device,ABS_Z);
#endif
		libevdev_free(device);
		xiSetProp(devid,aABS,aFloat,&devABS,NdevABS,0);
	}
	close(fd);
ret:
	if (grabcnt && (pi[p_safe]&2)) _Xgrab();
}
#endif

float matrix[9] = {0,0,0,0,0,0,0,0,0};
static void map_to(){
	float x=minf2.x,y=minf2.y,w=minf2.width,h=minf2.height,dx=pf[p_touch_add],dy=pf[p_touch_add];
	_short m = 1;
	if (pa[p_touch_add] && pa[p_touch_add][0] == '+' && pa[p_touch_add][1] == 0) {
		if (minf2.mwidth && devABS[0]!=0) dx = (devABS[0] - minf2.mwidth)/2;
		if (minf2.mheight && devABS[1]!=0) dy = (devABS[1] - minf2.mheight)/2;
	}
	if (dx!=0 && minf2.mwidth) {
		float b = (w/minf2.mwidth)*dx;
		x-=b;
		w+=b*2;
	}
	if (dy!=0 && minf2.mheight) {
		float b = (h/minf2.mheight)*dy;
		y-=b;
		h+=b*2;
	}
	x/=scr_width;
	y/=scr_height;
	w/=scr_width;
	h/=scr_height;
	switch (minf2.rotation) {
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
	xiSetProp(devid,aMatrix,aFloat,&matrix,9,4);
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

static void clearCache(int devid){
	XIDeleteProperty(dpy,devid,aDevMonCache);
}

XRRScreenResources *xrrr = NULL;
XRRCrtcInfo *cinf = NULL;
XRROutputInfo *oinf = NULL;
int nout = -1;

static _short xrMons(){
next:
	if (cinf) {
		XRRFreeCrtcInfo(cinf);
		XRRFreeOutputInfo(oinf);
		cinf = NULL;
		oinf = NULL;
	}
	if (xrrr) while (++nout<xrrr->noutput) {
		if (!(minf.out = xrrr->outputs[nout]) || !(oinf = XRRGetOutputInfo(dpy, xrrr, minf.out))) continue;
		if (oinf->connection == RR_Connected && (minf.crt = oinf->crtc) && (cinf=XRRGetCrtcInfo(dpy, xrrr, minf.crt))) break;
		XRRFreeOutputInfo(oinf);
		oinf = NULL;
	}
	if (!oinf) {
		nout = -1;
		return 0;
	}
	minf.name = XInternAtom(dpy, oinf->name, False);
	minf.width = minf.width0 = cinf->width;
	minf.height = minf.height0 = cinf->height;
	minf.mwidth = oinf->mm_width;
	minf.mheight = oinf->mm_height;
	minf.x = cinf->x;
	minf.y = cinf->y;
	minf.rotation = cinf->rotation;
#ifdef _BACKLIGHT
	xrChkProp(minf.out,aBl,&minf.bl);
#endif
	XRRModeInfo *m,*m0 = NULL,*m1 = NULL;
	RRMode id = cinf->mode;
	RRMode id0 = id + 1;
	unsigned long i;
	if (oinf->npreferred && id != (id0 = oinf->modes[0])) for (i=0; i<xrrr->nmode; i++) {
		m = &xrrr->modes[i];
		if (m->id == id) {
			m1 = m;
			if (m0) break;
		} else if (m->id == id0) {
			m0 = m;
			if (m1) break;
		}
	} else for (i=0; i<xrrr->nmode; i++) {
		m = &xrrr->modes[i];
		if (m->id == id) {
			m1 = m;
			if (id0 == id) m0 = m;
			break;
		}
	}
	if ((pi[p_safe]&256)) m0 = m1;
	if (m1) {
		minf.width = m1->width;
		minf.height = m1->height;
	}
	if (m0) {
		minf.width0 = m0->width;
		minf.height0 = m0->height;
	}
	minf.width1 = minf.width0;
	minf.height1 = minf.height0;
	if (minf.rotation & (RR_Rotate_90|RR_Rotate_180)) {
		i = minf.width;
		minf.width = minf.height;
		minf.height = i;
		i = minf.width0;
		minf.width0 = minf.height0;
		minf.height0 = i;
		i = minf.mwidth;
		minf.mwidth = minf.mheight;
		minf.mheight = i;
	}
	minf.x2 = minf.x + minf.width - 1;
	minf.y2 = minf.y + minf.height - 1;
	return 1;
}


unsigned int pan_x,pan_y,pan_cnt;
static void _pan(minf_t *m) {
	if (pi[p_safe]&64 || !m->crt) return;
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
	pan_y = max(pan_y,p->top + m->height0 + p->border_top + p->border_bottom);
	pan_x = max(pan_x,p->left + m->width0 + p->border_left + p->border_right);
	XRRFreePanning(p);
}

#ifdef XSS
static void monFullScreen(int x, int y) {
	if (!xrr || !(xrrr = XRRGetScreenResources(dpy,root))) return;
	while (xrMons()) {
		Atom ct = 0;
		if (!xrGetProp(minf.out,aCType,XA_ATOM,&ct,1,0) || !ct) continue;
		XRRPropertyInfo *pinf = NULL;
		Atom ct1 = 0;
		xrGetProp(minf.out,aCType1,XA_ATOM,&ct1,1,0);
		if (noXSS && x>=minf.x && x<=minf.x2 && y>=minf.y && y<=minf.y2
		    && (pinf = XRRQueryOutputProperty(dpy,minf.out,aCType))
		    && !pinf->range) {
			int i;
			for (i=0; i<pinf->num_values; i++) {
				if (pinf->values[i]==aCTFullScr) {
					if (ct != ct1) {
						//if (!ct1)
						    XRRConfigureOutputProperty(dpy,minf.out,aCType1,False,pinf->range,pinf->num_values,pinf->values);
						xrSetProp(minf.out,aCType1,XA_ATOM,&ct,1,0);
					}
					ct1 = aCTFullScr;
					break;
				}
			}
		}
		if (pinf) XFree(pinf);
		if (ct1 && ct != ct1) {
			DBG("monitor: %s content type: %s",oinf->name,XGetAtomName(dpy,ct1));
			xrSetProp(minf.out,aCType,XA_ATOM,&ct1,1,0);
		}
	}
	XRRFreeScreenResources(xrrr);
	xrrr = NULL;
}
#endif

// mode: 0 - x,y, 1 - mon, 2 - mon_sz
// mode 0 need only for scroll resolution
// mode 1 & 2 - for map-to-output too
static void getRes(int x, int y, _short mode){
	int i,j;
	_int found = 0, nm = 0;
	RROutput prim = 0;
	Atom name=0,name0 = 0;

//	XGetWindowAttributes(dpy, DefaultRootWindow(dpy), &wa);
	memset(&minf1,0,sizeof(minf1));
	memset(&minf2,0,sizeof(minf2));
	minf2.mwidth = scr_mwidth;
	minf2.mheight = scr_mheight;
	minf2.width = scr_width;
	minf2.height = scr_height;

	if(mon) {
		if (mode == 1) name=mon;
	} else if (devid && xiGetProp(devid,aDevMon,XA_ATOM,NULL,1,0)) name = *(Atom*)ret;

#ifdef USE_EVDEV
	if (mode == 2) {
		getEvRes();
		if ((devABS[0] == 0 || devABS[1] == 0) && !name) {
			_grabX();
			goto found;
		}
		// if (mode==0) ... get real resolution from matrix and return
	}
#endif
	// try dont lock for libevent, but still unsure (got one fd lock)

	_grabX();

	if (!xrr || !(xrrr = XRRGetScreenResources(dpy,root))) goto noxrr;
	RROutput prim0 = prim = XRRGetOutputPrimary(dpy, root);
	if ((pi[p_safe]&128)) {
		pi[p_safe]=(pi[p_safe]|(32|64)) ^ (prim?32:64);
		prim0 = 0;
	}
	if (!(pi[p_safe]&32)) prim0 = 0;
	while (xrMons()) {
		nm++;
		if((!found
#ifdef USE_EVDEV
		    || (mon_sz && found == 1)
#endif
		    ) &&
		    ((!mode && !found && !strict)
#ifdef USE_EVDEV
			    || (mon_sz && !name && (
				(minf.mwidth <= devABS[0] && devABS[0] - minf.mwidth < mon_grow
				    && minf.mheight <= devABS[1] && devABS[1] - minf.mheight < mon_grow)
				|| (minf.mheight <= devABS[0] && devABS[0] - minf.mheight < mon_grow
				    && minf.mwidth <= devABS[1] && devABS[1] - minf.mwidth < mon_grow)
			    ))
#endif
			    || (mode == 1 && (pi[p_mon] == nm))
			    || (name && name == minf.name)
		    ) &&
		    ( mode || (x >= minf.x && y >= minf.y && x <= minf.x2 && y <= minf.y2)) &&
		    (!found++ || !mode)) minf2 = minf;
		else if (!minf.mheight
//			|| (minf1.height && prim0)
			) continue;
		if ((prim0 == minf.out) || (!prim0 && minf.mheight > minf1.mheight)) minf1 = minf;
	}
noxrr:
#ifdef USE_XINERAMA
	if (!found && xir && XineramaIsActive(dpy) && pi[p_mon]) {
		int n = 0;
		i=pi[p_mon]-1;
		XineramaScreenInfo *s = XineramaQueryScreens(dpy, &n);
		if (i>=0 && i<n) {
			minf2.x = s[i].x_org;
			minf2.y = s[i].y_org;
			minf2.width = s[i].minf2.width;
			minf2.height = s[i].minf2.height;
			minf2.rotation = 0;
			minf2.x2 = minf2.x + minf2.width - 1;
			minf2.y2 = minf2.y + minf2.height - 1;
		}
		XFree(s);
	}
#endif
found:
#ifdef USE_EVDEV
	// workaround for some broken video drivers
	// we can
	if ((!found || !minf2.mwidth || !minf2.mheight)
	    && (pf[p_touch_add] != 0 || pf[p_res]<0)
	    ) {
		if (mode != 2 && devid) getEvRes();
		if (!devid && !resDev) resDev=1;
		// +.5 ?
		if (devABS[0]) minf2.mwidth = devABS[0];
		if (devABS[1]) minf2.mheight = devABS[1];
		if (!minf1.mheight && minf1.height) minf1.mheight = minf2.mheight;
		if (!minf1.mwidth && minf1.width) minf1.mwidth = minf2.mwidth;
	}
#else
	if (devid) {
		devABS[0] = minf2.mwidth;
		devABS[1] = minf2.mheight;
		xiSetProp(devid,aABS,aFloat,&devABS,NdevABS,2);
	}
#endif
	if (devid) xiSetProp(devid,aDevMonCache,XA_ATOM,&minf2.name,1,2);
	if (minf2.mwidth > minf2.mheight && minf2.width < minf2.height && minf2.mheight) {
		i = minf2.mwidth;
		minf2.mwidth = minf2.mheight;
		minf2.mheight = i;
	}
	if (mode==2) {
		 if (found==1) map_to();
		 else resXY = pf[p_res]<0;
	}
	if (pf[p_res]<0) {
		if (!minf2.mwidth && !minf2.mheight) {
			resX = resY = -pf[p_res];
			ERR("Screen dimensions in mm unknown. Use resolution in dots.");
		} else {
			if (minf2.mwidth) resX = (0.+scr_width)/minf2.mwidth*(-pf[p_res]);
			resY = minf2.mheight ? (0.+scr_height)/minf2.mheight*(-pf[p_res]) : resX;
			if (!minf2.mwidth) resX = resY;
		}
	}
	if (!xrr || !(xrrr = xrrr ? : XRRGetScreenResources(dpy,root))) goto noxrr1;
	if (!(pi[p_safe]&32) && minf1.out  && minf1.out != prim) {
		DBG("primary output %lu",minf1.out);
		XRRSetOutputPrimary(dpy,root,prim = minf1.out);
	}
	_short do_dpi = minf1.mheight && minf1.mwidth && !(pi[p_safe]&16);
	if ((do_dpi || !(pi[p_safe]&64))) {
		double dpmh, dpmw;
		pan_x = pan_y = pan_cnt = 0;
		cinf = NULL;
		if (do_dpi) {
			dpmh = (.0 + minf1.height) / minf1.mheight;
			dpmw = (.0 + minf1.width) / minf1.mwidth;
			// auto resolution usual equal physical propotions
			// in mode resolution - use one max density
		}
		if (minf1.crt != minf2.crt) _pan(&minf1);
		if (nm != ((!!minf1.crt) + (minf2.crt && minf1.crt != minf2.crt)))
		    while (xrMons()) {
			if (minf.crt != minf1.crt && minf.crt != minf2.crt) {
				if (do_dpi && minf.mwidth && minf.mheight) {
					if ( !(minf.width == minf1.width && minf.height == minf1.height && minf.mwidth == minf1.mwidth && minf.mheight == minf1.mheight) &&
					     !(minf.width == minf2.width && minf.height == minf2.height && minf.mwidth == minf2.mwidth && minf.mheight == minf2.mheight))
					fixMonSize(&minf, &dpmw, &dpmh);
				}
				_pan(&minf);
			}
		}
		_pan(&minf2);

		if (do_dpi) {
			int mh, mw;
			if (!pan_x)
				pan_x = scr_width;
			if (!pan_y)
				pan_y = scr_height;
			if (pan_cnt && (pan_x != scr_width || pan_y != scr_height)) {
				XFlush(dpy);
				XSync(dpy,False);
			}

			if (minf1.crt != minf2.crt) fixMonSize(&minf2, &dpmw, &dpmh);
			fixMonSize(&minf1, &dpmw, &dpmh);
rep_size:
			if (!(pi[p_safe]&512))
				if (dpmh > dpmw) dpmw = dpmh;
				else dpmh = dpmw;

			mh = pan_y / dpmh + .5;
			mw = pan_x / dpmw +.5;
    
			if (mw != scr_mwidth || mh != scr_mheight || pan_y != scr_height || pan_x != scr_width) {
				DBG("dpi %.1fx%.1f -> %.1fx%.1f dots/mm %ix%i/%ix%i -> %ix%i/%ix%i",25.4*scr_width/scr_mwidth,25.4*scr_height/scr_mheight,25.4*pan_x/mw,25.4*pan_y/mh,scr_width,scr_height,scr_mwidth,scr_mheight,pan_x,pan_y,mw,mh);
				_error = 0;
				XRRSetScreenSize(dpy, root, pan_x, pan_y, mw, mh);
				if (pan_x != scr_width || pan_y != scr_height) {
					XFlush(dpy);
					XSync(dpy,False);
					if (_error) {
						pan_x = scr_width;
						pan_y = scr_height;
						goto rep_size;
					}
				}
			}
		}
	}
noxrr1:
	_ungrabX();
	if (xrrr) XRRFreeScreenResources(xrrr);
	xrrr = NULL;
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
_short showPtr, oldShowPtr = 0, curShow;
_int tdevs = 0, tdevs2=0;
//#define floating (pi[p_floating]==1)
_short __init = 1;
static void getHierarchy(){
	static XIAttachSlaveInfo ca = {.type=XIAttachSlave};
	static XIDetachSlaveInfo cf = {.type=XIDetachSlave};
	static XIAddMasterInfo cm = {.type = XIAddMaster, .name = "TouchScreen", .send_core = 0, .enable = 1};
	static XIRemoveMasterInfo cr = {.type = XIRemoveMaster, .return_mode = XIFloating};
	int i,j,ndevs2,nrel,nkbd,m=0;
	short grabbed = 0, ev2=0;

	if (!resDev) {
		if (mon) getRes(0,0,1);
#ifdef USE_EVDEV
		else if (mon_sz) {
			resXY = 0;
			ev2 = ev2 && abs;
		}
#endif
	}
	if (!ev2 && (pi[p_safe]&7)==5) {_grabX();grabbed=1;} // just balance grab count
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
					ximask.mask=(void*)&ximaskTouch;
					ximask.deviceid=m;
					XISelectEvents(dpy, root, &ximask, 1);
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
		short t = 0, rel = 0, abs = 0, scroll = 0;
		busy = 0;
		clearCache(devid);
		if (__init) {
			XIDeleteProperty(dpy,devid,aABS);
		}
		switch (d2->use) {
		    case XIFloatingSlave: break;
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
				break;
#undef e
#define e ((XIValuatorClassInfo*)cl)
			    case XIValuatorClass:
				switch (e->mode) {
				    case Relative: rel=1; break;
				    case Absolute: abs=1; break;
				}
				break;
#undef e
#define e ((XIScrollClassInfo*)cl)
			    case XIScrollClass:
				scroll=1;
				break;
			}
		}
		if (pa[p_device] && *pa[p_device]) {
			if (!strcmp(pa[p_device],d2->name) || pi[p_device] == devid) {
				scroll = 0;
				t = 1;
			} else {
				scroll = 1;
				//t = 0;
				goto skip_map;
			}
		}
		if (abs && !resDev) {
			if (mon) map_to();
#ifdef USE_EVDEV
			else if (mon_sz) getRes(0,0,2);
#endif
		}
skip_map:
		tdevs+=t;
		nrel+=rel && !(abs || t);
		// exclude touchscreen with scrolling
		void *c = NULL;
		cf.deviceid = ca.deviceid = ximask.deviceid = devid;
		ximask.mask = NULL;
		switch (d2->use) {
		    case XIFloatingSlave:
			if (!t && !scroll) continue;
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
					ximask.mask=(void*)&ximask0;
				} else {
					ximask.mask=(void*)&ximaskTouch;
				}
				break;
			}
			break;
		    case XISlavePointer:
			if (!t) ximask.mask = (void*)(showPtr ? &ximask0 : &ximaskButton);
			else if (scroll) ximask.mask = (void*)(showPtr ? &ximaskTouch : &ximask0);
			else {
				tdevs2++;
				switch (pi[p_floating]) {
				    case 1:
					ximask.mask=(void*)&ximaskTouch;
					XIGrabDevice(dpy,devid,root,0,None,XIGrabModeSync,XIGrabModeTouch,False,&ximask);
					ximask.mask=NULL;
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
//			continue;
		}
		if (pi[p_safe]&1) {
			if (!(c || ximask.mask)) continue;
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
		if (c) XIChangeHierarchy(dpy, c, 1);
		if (ximask.mask) XISelectEvents(dpy, root, &ximask, 1);
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
	if (grabbed) _ungrabX();
	tdevs -= tdevs2;
	if (!resDev) resDev=1;
	__init = 0;
}

static void setShowCursor(){
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
	scr_width = DisplayWidth(dpy,screen);
	scr_height = DisplayHeight(dpy,screen);
	scr_mwidth = DisplayWidthMM(dpy,screen);
	scr_mheight = DisplayHeightMM(dpy,screen);
	resDev = 0;
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
	if (win!=root && getProp(win,aKbdGrp,XA_CARDINAL,sizeof(CARD32)))
		grp1 = *(CARD32*)ret;
	while (grp1 != grp && XkbLockGroup(dpy, XkbUseCoreKbd, grp1)!=Success && grp1) {
		XDeleteProperty(dpy,win,aKbdGrp);
		grp1 = 0;
	}
#ifdef XSS
	getProp(win,aWMState,XA_ATOM,sizeof(Atom));
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
	if (getProp(root,aActWin,XA_WINDOW,sizeof(Window)))
		win1 = *(Window*)ret;
}

int (*oldxerrh) (Display *, XErrorEvent *);
static int xerrh(Display *dpy, XErrorEvent *err){
	_error = True;
	switch (err->error_code) {
	    case BadWindow: win1 = None; break;
#ifdef XTG
	    case BadAccess: // second XI_Touch* root listener
		oldShowPtr |= 2;
		showPtr = 1;
		break;
	    case BadMatch:
		// XShowCursor() before XHideCursor()
		if (err->request_code == xfopcode) {
			if (err->minor_code == X_XFixesShowCursor) {
				curShow = 1;
				oldShowPtr |= 1;
			}
			return 0;
		}
		// RRSetScreenSize() on bad XRandr state;
		break;
#endif
	    default:
#ifdef XTG
		if (err->error_code==xierror) break; // changes on device list
old:
#endif
		oldxerrh(dpy,err);
	}
#ifdef XTG
	static int oldErr = 0;
	if (oldErr != err->error_code) {
		oldErr=err->error_code;
		ERR("XError: type=%i XID=0x%lx serial=%lu error_code=%i%s request_code=%i minor_code=%i xierror=%i",
			err->type, err->resourceid, err->serial, oldErr,
			oldErr == xierror ? "=XI" : oldErr == xrerror ?"=XrandR":"s",
			err->request_code, err->minor_code, xierror);
	}
#endif
	return 0;
}

static void init(){
	int evmask = PropertyChangeMask;

	screen = DefaultScreen(dpy);
	root = DefaultRootWindow(dpy);
//	XGetWindowAttributes(dpy, root, &wa);
	aActWin = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	aKbdGrp = XInternAtom(dpy, "_KBD_ACTIVE_GROUP", False);
	aXkbRules = XInternAtom(dpy, "_XKB_RULES_NAMES", False);
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
	aDevMon = XInternAtom(dpy, "xtg output", False);	aDevMonCache = XInternAtom(dpy, "xtg output cached", False);
#ifdef _BACKLIGHT
	aBl = XInternAtom(dpy, RR_PROPERTY_BACKLIGHT, False);
#endif
#ifdef USE_EVDEV
	aNode = XInternAtom(dpy, "Device Node", False);
	aABS = XInternAtom(dpy, "Device libinput ABS", False);
#else
	aABS = XInternAtom(dpy, "xtg ABS", False);
#endif
#ifdef XSS
	aCType = XInternAtom(dpy, "content type", False);
	aCType1 = XInternAtom(dpy, "xtg saved content type", False);
	if (pa[p_content_type]) aCTFullScr = XInternAtom(dpy, pa[p_content_type], False);
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
	XISelectEvents(dpy, root, &ximask, 1);
	XIClearMask(ximask0, XI_HierarchyChanged);
#ifdef DEV_CHANGED
	XIClearMask(ximask0, XI_DeviceChanged);
#endif

	xrevent = xrerror
#ifdef USE_XINERAMA
	    = xirevent = xirerror
#endif
	    = -1;
	_grabX();
	if (pf[p_res]<0 || mon || mon_sz) {
		if (xrr = XRRQueryExtension(dpy, &xrevent, &xrerror)) {
			xrevent += RRScreenChangeNotify;
			XRRSelectInput(dpy, root, RRScreenChangeNotifyMask);
		};
#ifdef USE_XINERAMA
		xir = XineramaQueryExtension(dpy, &xirevent, &xirerror);
#endif
	}
	_scr_size();
	//forceUngrab();
#ifdef XSS
	if (XScreenSaverQueryExtension(dpy, &xssevent, &xsserror)) {
		XScreenSaverSelectInput(dpy, root, ScreenSaverNotifyMask);
		XScreenSaverInfo *x = XScreenSaverAllocInfo();
		if (x && XScreenSaverQueryInfo(dpy, root, x) == Success && x->state == ScreenSaverOn)
			timeSkip--;
		XFree(x);
	} else xssevent = -1;
#endif
	if (XQueryExtension(dpy, "XFIXES", &xfopcode, &xfevent, &xferror)) {
//		XFixesSelectCursorInput(dpy,root,XFixesDisplayCursorNotifyMask);
	}
#endif
	XSelectInput(dpy, root, evmask);
	oldxerrh = XSetErrorHandler(xerrh);
	
	win = root;
	win1 = None;
	grp = NO_GRP;
	grp1 = 0;
	rul = NULL;
	rul2 =NULL;
	ret = NULL;
#ifdef XTG
	curShow = 0;
	showPtr = 1;
	XFixesShowCursor(dpy,root);
#ifdef XSS
	monFullScreen(0,0);
#endif
#endif
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
		switch (ev.type) {
#ifdef XTG
		    case GenericEvent:
			if (ev.xcookie.extension == xiopcode) {
//				fprintf(stderr,"ev %i\n",ev.xcookie.evtype);
				if (ev.xcookie.evtype < XI_TouchBegin) switch (ev.xcookie.evtype) {
#ifdef DEV_CHANGED
				    case XI_DeviceChanged:
#undef e
#define e ((XIDeviceChangedEvent*)ev.xcookie.data)
					if (XGetEventData(dpy, &ev.xcookie)) {
						TIME(T,e->time);
						int r = e->reason;
						//fprintf(stderr,"dev changed %i %i (%i)\n",r,e->deviceid,devid);
						XFreeEventData(dpy, &ev.xcookie);
						if (r == XISlaveSwitch) goto ev2;
					}
					resDev = 0;
					oldShowPtr |= 2;
					continue;
#endif
#undef e
#define e ((XIPropertyEvent*)ev.xcookie.data)
				    case XI_PropertyEvent:
					if (XGetEventData(dpy, &ev.xcookie)) {
						TIME(T,e->time);
						Atom p = e->property;
						int devid1 = e->deviceid;
						XFreeEventData(dpy, &ev.xcookie);
						if (
							//devid!=devid1 ||
							p==aMatrix
							|| p==aABS
							|| p==aDevMonCache
							) goto ev2;
						if (p==aDevMon) clearCache(devid1);
					}
				    case XI_HierarchyChanged:
					resDev = 0;
					oldShowPtr |= 2;
					continue;
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
				if (resDev != devid) {
					// slow for multiple touchscreens
					if (resXY && ((pi[p_safe]&1024)
					    || xiGetProp(devid,aABS,aFloat,&devABS,NdevABS,1)!=2
					    || !xiGetProp(devid,aDevMonCache,XA_ATOM,&minf2.name,1,2)
					    //|| !xiGetProp(devid,aDevMon,XA_ATOM,&minf2.name,1,2)
					    //|| !xiGetProp(devid,aMatrix,aFloat,&matrix,9,2)
						)) getRes(x2,y2,0);
					resDev = devid;
				}
				g = ((int)x2 <= minf2.x) ? BUTTON_RIGHT : ((int)x2 >= minf2.x2) ? BUTTON_LEFT : ((int)y2 <= minf2.y) ? BUTTON_UP : ((int)y2 >= minf2.y2) ? BUTTON_DOWN : 0;
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
			//fprintf(stderr,"Prop %s\n",XGetAtomName(dpy,e.atom));
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
/*
		    case CreateNotify:
		    case UnmapNotify:
		    case MapNotify:
		    case ReparentNotify:
		    case DestroyNotify:
			goto ev;
			break;
*/
#ifdef XTG
#undef e
#define e (ev.xconfigure)
		    case ConfigureNotify:
			if (xrr || !XRRUpdateConfiguration(&ev)) goto ev;
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
			}
#ifdef XTG
#ifdef XSS
#undef e
#define e ((XScreenSaverNotifyEvent*)&ev)
			else if (ev.type == xssevent) {
				TIME(T,e->time); // no sense for touch
				switch (e->state) {
//				    case ScreenSaverDisabled:
				    case ScreenSaverOff:
					timeSkip = e->time;
					break;
				}
			}
#endif
#undef e
#define e ((XRRScreenChangeNotifyEvent*)&ev)
			else if (ev.type == xrevent) {
				TIME(T,e->timestamp > e->config_timestamp ? e->timestamp : e->config_timestamp);
				XRRUpdateConfiguration(&ev);
				_scr_size();
				//scr_rotation = e->rotation;
			}
#undef e
#define e ((XFixesCursorNotifyEvent*)&ev)
//			else if (xfevent && ev.type == xfevent + XFixesCursorNotify) {
//				    TIME(T,e->timestamp);
//			}
#endif
//			else fprintf(stderr,"ev? %i\n",ev.type);
			break;
		}
	}
}
