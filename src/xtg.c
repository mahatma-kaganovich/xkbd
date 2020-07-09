/*
	xtg v1.25 - per-window keyboard layout switcher [+ XSS suspend].
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
	-h to help

	(C) 2019-2020 Denis Kaganovich, Anarchy license
*/
#ifndef NO_ALL
#define XSS
#define XTG
//#define TOUCH_ORDER
//#define USE_EVDEV
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

typedef uint_fast8_t _short;
typedef uint_fast32_t _int;

Display *dpy = 0;
Window win, win1;
int screen;
XWindowAttributes wa;
Atom aActWin, aKbdGrp, aXkbRules;
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

#ifdef XTG
int devid = 0;
int xiopcode, xierror;
Atom aFloat,aMatrix;
#ifdef USE_EVDEV
Atom aNode;
#endif
#define MASK_LEN XIMaskLen(XI_LASTEVENT)
typedef unsigned char xiMask[MASK_LEN];
xiMask ximaskButton = {}, ximaskTouch = {}, ximask0 = {};
XIEventMask ximask = { .deviceid = XIAllDevices, .mask_len = MASK_LEN, .mask = (void*)&ximask0 };

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
int xrr, xrevent, xrerror;
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
		m = &((*m)->gg[i&7]);
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
#define MAX_PAR 10
char *pa[MAX_PAR] = {};
// android: 125ms tap, 500ms long
#define PAR_DEFS { 0, 1, 2, 1000, 2, -2, 0, 1, DEF_RR, 0}
int pi[] = PAR_DEFS;
double pf[] = PAR_DEFS;
char *ph[MAX_PAR] = {
	"touch device 0=auto",
	"min fingers",
	"max fingers",
	"button 2|3 hold time, ms",
	"min swipe x/y or y/x",
	"swipe size (>0 - dots, <0 - -mm)",
	"add to coordinates (to round to integer)",
	"1=floating devices, 0=master device (visual artefacts, but enable native masters touch clients)",
	"RandR monitor name\n"
	"		or number 1+\n"
#ifdef USE_EVDEV
	"		or negative -<x> to find monitor, using evdev touch input size, grow +x\n"
#endif
	"			for (all|-d) absolute pointers (=xinput map-to-output)",
	"map-to-output add field around screen, mm (if -R)",
};
char pc[] = "d:m:M:t:x:r:e:f:R:a:h";
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

static Bool getRules(){
	Atom t;
	int f, i;
	unsigned long b;

	XFree(rul);
	rul = NULL;
	rulLen = 0;
	n1 = 0;
	if (XGetWindowProperty(dpy,wa.root,aXkbRules,0,256,False,XA_STRING,&t,&f,&rulLen,&b,&rul)==Success && rul && rulLen>1 && t==XA_STRING) {
		rul2 = rul;
		return True;
	}
	rul2 = NULL;
	return False;
}

static int getProp(Window w, Atom prop, Atom type, int size){
	Atom t;
	int f;
	unsigned long b;

	XFree(ret);
	ret = NULL;
	if (!(XGetWindowProperty(dpy,w,prop,0,1024,False,type,&t,&f,&n,&b,&ret)==Success && f>>3 == size && ret))
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
static void WMState(Atom *states, short nn){
	short i;
	noXSS1 = False;
	for(i=0; i<nn; i++) {
		if (states[i] == aFullScreen) {
			noXSS1 = True;
			break;
		}
	}
	if (noXSS1!=noXSS) XScreenSaverSuspend(dpy,noXSS=noXSS1);
}
#endif

#ifdef XTG

int scrX1,scrY1,scrX2,scrY2;
int width, height, mwidth, mheight, rotation;
_short resXY;
_short mon = 0;
_int mon_sz = 0;

#if 0
#define TMUL1000(x) (x*1000)
#define TDIV1000(x) (x/1000)
#else
#define TMUL1000(x) (x<<10)
#define TDIV1000(x) (x>>10)
#endif

static int _delay(Time delay){
	if (XPending(dpy)) return 0;
	struct timeval tv;
	fd_set rs;
	int fd = ConnectionNumber(dpy);
	tv.tv_sec = TDIV1000(delay);
	tv.tv_usec = TMUL1000(delay);
	FD_ZERO(&rs);
	FD_SET(fd,&rs);
	return !Select(fd+1, &rs, 0, 0, &tv);
}

#ifdef USE_EVDEV
double devX = 0, devY = 0;
static double _evsize(struct libevdev *dev, int c) {
	double r = 0;
	struct input_absinfo *x = libevdev_get_abs_info(dev, c);
	if (x) r = (r + x->maximum - x->minimum)/x->resolution;
	return r;
}
static void getEvRes(){
	Atom t;
	char *name;
	int f;
	unsigned long n,b;
	devX = devY = 0;
	if (XIGetProperty(dpy,devid,aNode,0,1024,False,XA_STRING,&t,&f,&n,&b,&name) != Success) return;
	int fd = open(name, O_RDONLY|O_NONBLOCK);
	XFree(name);
	if (fd < 0) return;
	struct libevdev *device;
	if (!libevdev_new_from_fd(fd, &device)){
		devX = _evsize(device,ABS_X);
		devY = _evsize(device,ABS_Y);
		libevdev_free(device);
	}
	close(fd);
}
#endif

static void map_to(){
	float x=scrX1,y=scrY1,w=width,h=height;
	_short m = 1;
	if (pf[p_touch_add] != 0) {
		float b;
		b=(w/mwidth)*pf[p_touch_add];
		x-=b;
		w+=b*2;
		b=(h/mheight)*pf[p_touch_add];
		y-=b;
		h+=b*2;
	}
	x/=wa.width;
	y/=wa.height;
	w/=wa.width;
	h/=wa.height;
	switch (rotation) {
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
	Atom t;
	int f;
	unsigned long n,b;
	void *d = NULL;
	if (XIGetProperty(dpy,devid,aMatrix,0,9,False,aFloat,&t,&f,&n,&b,&d) == Success
	    && t==aFloat && f==32 && n == 9 && !b)
		XIChangeProperty(dpy,devid,aMatrix,aFloat,32,PropModeReplace,(void*)&matrix,9);
	XFree(d);
}

// mode: 0 - x,y, 1 - mon, 2 - mon_sz
// mode 0 need only for scroll resolution
// mode 1 & 2 - for map-to-output too
static void getRes(int x, int y, _short mode){
	int i;
	_int found = 0;

	XGetWindowAttributes(dpy, DefaultRootWindow(dpy), &wa);
	mwidth = DisplayWidthMM(dpy, screen);
	mheight = DisplayHeightMM(dpy, screen);
	width = wa.width;
	height = wa.height;
	scrX1 = 0;
	scrY1 = 0;
#ifdef USE_EVDEV
	if (mode == 2) {
		getEvRes();
		if (devX == 0 || devY == 0) goto found;
		// if (mode==0) ... get real resolution from matrix and return
	}
#endif
	XRRScreenResources *xrrr;
	if (xrr && (xrrr = XRRGetScreenResources(dpy,wa.root))) {
		int n = 0;
		for (i = 0; i < xrrr->noutput && (!found
#ifdef USE_EVDEV
		    || (mon_sz != 0 && found == 1)
#endif
		    ); i++) {
			XRROutputInfo *oinf = XRRGetOutputInfo(dpy, xrrr, xrrr->outputs[i]);
			if (!oinf) continue;
			XRRCrtcInfo *cinf;
			if (oinf->crtc && oinf->connection == RR_Connected
			    && ((!mode && !found)
#ifdef USE_EVDEV
			      || (mon_sz != 0 && (
				(oinf->mm_width <= devX && devX - oinf->mm_width < mon_sz
				    && oinf->mm_height <= devY && devY - oinf->mm_height < mon_sz)
				|| (oinf->mm_height <= devX && devX - oinf->mm_height < mon_sz
				    && oinf->mm_width <= devY && devY - oinf->mm_width < mon_sz)
			      ))
#endif
			      || (mode == 1 && (pi[p_mon] == ++n || (pa[p_mon] && !strcmp(oinf->name,pa[p_mon]))))
			    )
			    && (cinf = XRRGetCrtcInfo(dpy, xrrr, oinf->crtc))
			    ) {
				if (mode || (x >= cinf->x && y >= cinf->y && x < cinf->x + cinf->width && y < cinf->y + cinf->height)) {
					if(!found++ || !mode) {
						scrX1 = cinf->x;
						scrY1 = cinf->y;
						mwidth=oinf->mm_width;
						mheight=oinf->mm_height;
						width = cinf->width;
						height = cinf->height;
						rotation = cinf->rotation;
					}
				}
				XRRFreeCrtcInfo(cinf);
			}
			XRRFreeOutputInfo(oinf);
		}
		XRRFreeScreenResources(xrrr);
	}
#ifdef USE_XINERAMA
	if (!found && xir && XineramaIsActive(dpy) && pi[p_mon]) {
		int n = 0;
		i=pi[p_mon]-1;
		XineramaScreenInfo *s = XineramaQueryScreens(dpy, &n);
		if (i>=0 && i<n) {
			scrX1 = s[i].x_org;
			scrY1 = s[i].y_org;
			width = s[i].width;
			height = s[i].height;
			rotation = 0;
		}
		XFree(s);
	}
#endif
found:
	scrX2 = scrX1 + width - 1;
	scrY2 = scrY1 + height - 1;
	if (mwidth > mheight && width < height && mheight) {
		i = mwidth;
		mwidth = mheight;
		mheight = i;
	}
#ifdef USE_EVDEV
	if (mode==2) {
		 if (found==1) map_to();
		 else resXY = pf[p_res]<0;
	}
#endif
	if (pf[p_res]<0) {
		if (!mwidth && !mheight) {
			resX = resY = -pf[p_res];
			fprintf(stderr,"Screen dimensions in mm unknown. Use resolution in dots.\n");
		} else {
			if (mwidth) resX = (0.+wa.width)/mwidth*(-pf[p_res]);
			resY = mheight ? (0.+wa.height)/mheight*(-pf[p_res]) : resX;
			if (!mwidth) resX = resY;
		}
	}
}


int xtestPtr, xtestPtr0;
_short showPtr = 1, oldShowPtr = 3, curShow = 1;
_int tdevs = 0, tdevs2=0;
#define floating pi[p_floating]
static void getHierarchy(){
	static XIAttachSlaveInfo ca = {.type=XIAttachSlave};
	static XIDetachSlaveInfo cf = {.type=XIDetachSlave};
	int i,j,ndevs2,nrel,nkbd,m=0,m0=0,k0=0;
	XIDeviceInfo *info2 = XIQueryDevice(dpy, XIAllDevices, &ndevs2);

	if (!resDev) {
		if (mon) getRes(0,0,1);
#ifdef USE_EVDEV
		else if (mon_sz != 0) resXY = 0;
#endif
	}
	if (!floating) {
	    for (i=0; i<ndevs2; i++) {
		XIDeviceInfo *d2 = &info2[i];
		devid = d2->deviceid;
		switch (d2->use) {
		    case XIMasterPointer:
			if (!m0) m0 = devid;
			else if (!m && !strncmp(d2->name,"TouchScreen ",12)) {
				m = devid;
//				XIUndefineCursor(dpy,m,wa.root);
//				XIDefineCursor(dpy,m,wa.root,None);
			}
			break;
		    case XIMasterKeyboard:
			if (!k0) k0 = devid;
			break;
		}
	    }
	    ca.new_master = m;
	}
	xtestPtr0 = xtestPtr = 0;
	tdevs2 = tdevs = 0;
	for (i=0; i<ndevs2; i++) {
		XIDeviceInfo *d2 = &info2[i];
		devid = d2->deviceid;
		short t = 0, rel = 0, abs = 0, scroll = 0;
		switch (d2->use) {
		    case XIFloatingSlave: break;
		    case XISlavePointer:
			if (ev.xcookie.extension == xiopcode && P != N && touch[N].deviceid == devid) N=TOUCH_P(N);
			if (!strstr(d2->name," XTEST ")) break;
			if (d2->attachment == m0) xtestPtr0 = devid;
			else if (d2->attachment == m) xtestPtr = devid;
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
			else if (mon_sz != 0) getRes(0,0,2);
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
			if (floating) tdevs2++;
			else if (m) c = &ca;
			break;
		    case XISlavePointer:
			if (!t) ximask.mask = (void*)(showPtr ? &ximask0 : &ximaskButton);
			else if (scroll) ximask.mask = (void*)(showPtr ? &ximaskTouch : &ximask0);
			else {
				tdevs2++;
				if (floating) {
					ximask.mask=&ximaskTouch;
					XIGrabDevice(dpy,devid,wa.root,0,None,XIGrabModeSync,XIGrabModeTouch,False,&ximask);
					ximask.mask=NULL;
				} else if (m && m != d2->attachment) c = &ca;
			}
			break;
//		    case XISlaveKeyboard:
//			nkbd++;
//		    default:
//			continue;
		}
		if (c) XIChangeHierarchy(dpy, c, 1);
		if (ximask.mask) XISelectEvents(dpy, wa.root, &ximask, 1);
	}
	if (!floating) {
		if (!m) {
			static XIAddMasterInfo cm = {.type = XIAddMaster, .name = "TouchScreen", .send_core = 0, .enable = 1};
			XIChangeHierarchy(dpy, &cm, 1);
		}
	}
	XIFreeDeviceInfo(info2);
	XFlush(dpy);
	tdevs -= tdevs2;
	if (!resDev) resDev=1;
}

static inline void setShowCursor(){
	oldShowPtr = showPtr;
	getHierarchy();
	if ((oldShowPtr & 2) && !showPtr) return;
	// "Hide" must be first!
	if (showPtr != curShow) {
		curShow = showPtr;
		if (!floating) return;
		if (showPtr) XFixesShowCursor(dpy, wa.root);
		else XFixesHideCursor(dpy, wa.root);
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
#endif

static void getWinGrp(){
	if (win1==None) {
		XGetInputFocus(dpy, &win1, &revert);
		if (win1==None) win1 = wa.root;
	}
	win = win1;
	grp1 = 0;
	if (win!=wa.root && getProp(win,aKbdGrp,XA_CARDINAL,sizeof(CARD32)))
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
	if (win!=wa.root)
		XChangeProperty(dpy,win,aKbdGrp,XA_CARDINAL,32,PropModeReplace,(unsigned char*) &grp1,1);
	grp = grp1;
}

static void getPropWin1(){
	if (getProp(wa.root,aActWin,XA_WINDOW,sizeof(Window)))
		win1 = *(Window*)ret;
}

int (*oldxerrh) (Display *, XErrorEvent *);
static int xerrh(Display *dpy, XErrorEvent *err){
	switch (err->error_code) {
	    case BadWindow: win1 = None; break;
#ifdef XTG
	    case BadAccess: // second XI_Touch* root listener
		oldShowPtr |= 2;
		showPtr = 1;
		break;
//	    case BadMatch: break; // XShowCursor() before XHideCursor()
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
		fprintf(stderr, "XError: type=%i XID=0x%lx serial=%lu error_code=%i%s request_code=%i minor_code=%i xierror=%i\n",err->type,err->resourceid,err->serial,err->error_code,err->error_code == xierror ? "=XI": "",err->request_code,err->minor_code,xierror);
	}
#endif
	return 0;
}


static void init(){
	int evmask = PropertyChangeMask;

	screen = DefaultScreen(dpy);
	XGetWindowAttributes(dpy, DefaultRootWindow(dpy), &wa);
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
	XISetMask(ximaskButton, XI_ButtonPress);
	XISetMask(ximaskButton, XI_ButtonRelease);
	XISetMask(ximaskButton, XI_Motion);

	XISetMask(ximaskTouch, XI_TouchBegin);
	XISetMask(ximaskTouch, XI_TouchUpdate);
	XISetMask(ximaskTouch, XI_TouchEnd);
	XISetMask(ximaskTouch, XI_PropertyEvent);

	XISetMask(ximask0, XI_HierarchyChanged);
	XISelectEvents(dpy, wa.root, &ximask, 1);
	XIClearMask(ximask0, XI_HierarchyChanged);

	if (!floating) {
		ximask.mask=&ximaskTouch;
		ximask.deviceid=XIAllMasterDevices;
		XISelectEvents(dpy, wa.root, &ximask, 1);
	}

	xrevent = xrerror
#ifdef USE_XINERAMA
	    = xirevent = xirerror
#endif
	    = -1;
	if (pf[p_res]<0 || mon || mon_sz != 0) {
		if (xrr = XRRQueryExtension(dpy, &xrevent, &xrerror)) {
			xrevent += RRScreenChangeNotify;
			XRRSelectInput(dpy, wa.root, RRScreenChangeNotifyMask);
		};
#ifdef USE_XINERAMA
		xir = XineramaQueryExtension(dpy, &xirevent, &xirerror);
#endif
#ifdef USE_EVDEV
	aNode = XInternAtom(dpy, "Device Node", False);
#endif
	}
#ifdef XSS
	if (XScreenSaverQueryExtension(dpy, &xssevent, &xsserror)) {
		xssevent += ScreenSaverNotify;
		XScreenSaverSelectInput(dpy, wa.root, ScreenSaverNotifyMask);
		XScreenSaverInfo *x = XScreenSaverAllocInfo();
		if (x && XScreenSaverQueryInfo(dpy, wa.root, x) == Success && x->state == ScreenSaverOn)
			timeSkip--;
		XFree(x);
	} else xssevent = -1;
#endif
#endif
	XSelectInput(dpy, wa.root, evmask);
	oldxerrh = XSetErrorHandler(xerrh);
	win = wa.root;
	win1 = None;
	grp = NO_GRP;
	grp1 = 0;
	rul = NULL;
	rul2 =NULL;
	ret = NULL;
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
		switch (sscanf(*a,"%i:%i:%127s",&x,&y,&k)) {
		    case 3:
			z = XKeysymToKeycode(dpy,XStringToKeysym(&k));
		    case 2:
			SET_BMAP(x,y,z);
			continue;
		}
		fprintf(stderr,"Error: invalid map item '%s', -h to help\n",*a);
		return 1;
	}
	resX = resY = pf[p_res] < 0 ? 1 : pf[p_res];
	if (pi[p_mon] < 0)
#ifdef USE_EVDEV
		mon_sz = -pi[p_mon]
#endif
		;
	else if (pa[p_mon] && *pa[p_mon]) mon = 1;
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
#ifdef XTG
		if (showPtr != oldShowPtr) setShowCursor();
#endif
ev:
#ifdef XTG
		if (timeHold) {
			Time t = T - timeHold;
			if (t >= pi[p_hold] || _delay(pi[p_hold] - t)) {
				to->g = BUTTON_HOLD;
				timeHold = 0;
				goto hold;
			}
		}
ev2:
#endif
		XNextEvent(dpy, &ev);
		switch (ev.type) {
#ifdef XTG
		    case GenericEvent:
			if (ev.xcookie.extension == xiopcode) {
				if (ev.xcookie.evtype < XI_TouchBegin) switch (ev.xcookie.evtype) {
				    case XI_PropertyEvent:
					resDev = 0;
				    case XI_HierarchyChanged:
					oldShowPtr |= 2;
					continue;
				    default:
					if (!showPtr) {
						showPtr = 1;
						if (!floating) XFixesShowCursor(dpy, wa.root);
					}
					continue;
				}
				if (!tdevs2 || !XGetEventData(dpy, &ev.xcookie)) goto ev;
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
				if (showPtr) {
					showPtr = 0;
					if (!floating) {
						XFixesHideCursor(dpy, wa.root);
						// reduce artefacts from cursors race
						XFlush(dpy);
						XWarpPointer(dpy, None, wa.root, 0, 0, 0, 0, x2, y2);
						XFlush(dpy);
					}
				}
				to = NULL;
				for(i=P; i!=N; i=TOUCH_N(i)){
					Touch *t1 = &touch[i];
					if (t1->touchid != e->detail || t1->deviceid != devid) continue;
					to = t1;
					break;
				}
				if (resDev != devid) {
					// slow for multiple touchscreens
					if (resXY) getRes(x2,y2,0);
					resDev = devid;
				}
				g = ((int)x2 <= scrX1) ? BUTTON_RIGHT : ((int)x2 >= scrX2) ? BUTTON_LEFT : ((int)y2 <= scrY1) ? BUTTON_UP : ((int)y2 >= scrY2) ? BUTTON_DOWN : 0;
				if (g) tt |= PH_BORDER;
				if (!to) {
					if (end) goto evfree;
					timeHold = 0;
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
						if (oldShowPtr) continue;
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
				}
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
			//if (e.window!=wa.root) break;
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
				resDev = 0;
				oldShowPtr |= 2;
			}
#endif
//			else fprintf(stderr,"ev? %i\n",ev.type);
			break;
		}
	}
}
