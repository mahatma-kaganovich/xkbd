/*
	xkbdd v1.14 - per-window keyboard layout switcher [+ XSS suspend].
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

	Touchscreen extensions (-DXTG):
	Auto hide/show mouse cursor on touchscreen.
	Swipe.
	Experimental.
	optional: ARGV[1] = device (unknown = disable gestures)

	(C) 2019-2020 Denis Kaganovich, Anarchy license
*/
#ifndef NO_ALL
#define XSS
#define XTG
#endif

#include <stdio.h>

#include <X11/Xproto.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>

#ifdef XSS
#include <X11/extensions/scrnsaver.h>
#endif

#ifdef XTG
//#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>

#include <X11/extensions/XTest.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#endif

#define NO_GRP 99


Display *dpy;
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
int xiopcode, xierror;
#define MASK_LEN XIMaskLen(XI_LASTEVENT)
typedef unsigned char xiMask[MASK_LEN];
xiMask ximaskButton = {}, ximaskTouch = {}, ximask0 = {};
XIEventMask ximask = { .deviceid = XIAllDevices, .mask_len = MASK_LEN, .mask = (void*)&ximask0 };
#endif

#ifdef XTG
#define TOUCH_SHIFT 5
#define TOUCH_MAX (1<<TOUCH_SHIFT)
#define TOUCH_MASK (TOUCH_MAX-1)
#define TOUCH_INC(x) (x=(x+1)&TOUCH_MASK)
#define TOUCH_DEC(x) (x=(x+TOUCH_MASK)&TOUCH_MASK)
typedef struct _Touch {
	int touchid,deviceid;
//	Time time;
	unsigned short n,g;
	double x,y;
	double tail;
} Touch;
Touch touch[TOUCH_MAX];
unsigned short P=0,N=0;

#define p_device 0
#define p_minfingers 1
#define p_maxfingers 2
#define p_max7 3
#define p_xy 4
#define p_res 5
#define MAX_PAR 6
char *pa[MAX_PAR] = {};
int pi[MAX_PAR];
double pf[MAX_PAR];
double pd[MAX_PAR] = { 0, 1, 2, 1, 2, 15 };
char *ph[MAX_PAR] = {
	"touch device 0=auto",
	"min fingers",
	"max fingers",
	"only buttons 4-7",
	"min swipe x/y or y/x",
	"dot per swipe",
};
#define res pf[p_res]
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
int xtestPtr, xtestKbd;
short showPtr = 1, oldShowPtr = 3, tdevs = 0, tdevs2=0, curShow = 1;
static void getHierarchy(int st){
	static XIAttachSlaveInfo ca = {.type=XIAttachSlave};
	static XIDetachSlaveInfo cf = {.type=XIDetachSlave};
	int i,j,ndevs2,nrel,nkbd;
	XIDeviceInfo *info2 = XIQueryDevice(dpy, XIAllDevices, &ndevs2);

	ca.new_master = 0;
	xtestPtr = xtestKbd = 0;
	tdevs2 = tdevs = 0;
	for (i=0; i<ndevs2; i++) {
		XIDeviceInfo *d2 = &info2[i];
		short t = 0, rel = 0, abs = 0, scroll = 0;
		switch (d2->use) {
		    case XIMasterPointer:
			if (!ca.new_master) ca.new_master = d2->deviceid;
		    case XIMasterKeyboard:
			continue;
		    case XISlavePointer:
			if (ev.xcookie.extension == xiopcode && P != N && touch[N].deviceid == d2->deviceid) TOUCH_DEC(N);
			if (!xtestPtr
//			    && !strcmp(d2->name,"Virtual core XTEST pointer")
			    ) {
				xtestPtr = d2->deviceid;
				continue;
			}
			if (!strcmp(d2->name,"Virtual core XTEST pointer")) continue;
			break;
		    case XISlaveKeyboard:
			if (!xtestKbd
//			    && !strcmp(d2->name,"Virtual core XTEST keyboard")
			    ) {
				xtestKbd = d2->deviceid;
				continue;
			}
			if (!strcmp(d2->name,"Virtual core XTEST keyboard")) continue;
			break;

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
			if (!strcmp(pa[p_device],d2->name) || pi[p_device] == d2->deviceid) {
				scroll = 0;
				t = 1;
			} else {
				scroll = 1;
				//t = 0;
			}
		}
		tdevs+=t;
		nrel+=rel && !(abs || t);
		// exclude touchscreen with scrolling
		void *c = NULL;
		cf.deviceid = ca.deviceid = ximask.deviceid = d2->deviceid;
		ximask.mask = NULL;
		switch (d2->use) {
		    case XIFloatingSlave:
			if (!t && !scroll) continue;
			if (st) {
				tdevs2++;
				ximask.mask = (void*)&ximaskTouch;
			} else c=&ca;
			break;
		    case XISlavePointer:
			if (!t) ximask.mask = (void*)(showPtr ? &ximask0 : &ximaskButton);
			else if (!scroll) c=&cf;
			else ximask.mask = (void*)(showPtr ? &ximaskTouch : &ximask0);
//			else ximask.mask = (void*)(showPtr ? &ximaskButton : &ximask0);
			break;
		    case XISlaveKeyboard:
			nkbd++;
		    default:
			continue;
		}
		if (c) XIChangeHierarchy(dpy, c, 1);
		if (ximask.mask) XISelectEvents(dpy, wa.root, &ximask, 1);
	}
	XIFreeDeviceInfo(info2);
	XFlush(dpy);
	tdevs -= tdevs2;
}

static inline void setShowCursor(){
	oldShowPtr = showPtr;
	getHierarchy(1);
	if ((oldShowPtr & 2) && !showPtr) return;
#if 0
#undef e
#define e ((XIDeviceEvent*)ev.xcookie.data)
	if (e) XWarpPointer(dpy, None, wa.root, 0, 0, 0, 0, e->root_x+0.5, e->root_y+0.5);
#endif
	// "Hide" must be first!
	if (showPtr == curShow) return;
	curShow = showPtr;
	if (showPtr) XFixesShowCursor(dpy, wa.root);
	else XFixesHideCursor(dpy, wa.root);
}

static void _signals(void *sig) {
	signal(SIGTERM,sig);
	signal(SIGINT,sig);
	signal(SIGABRT,sig);
	signal(SIGQUIT,sig);
}

static void sigterm(int sig) {
	_signals(SIG_DFL);
	opendpy();
	if (dpy) getHierarchy(0);
	exit(1);
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
	XISetMask(ximaskButton, XI_ButtonPress);
	XISetMask(ximaskButton, XI_ButtonRelease);
	XISetMask(ximaskButton, XI_Motion);

	XISetMask(ximaskTouch, XI_TouchBegin);
	XISetMask(ximaskTouch, XI_TouchUpdate);
	XISetMask(ximaskTouch, XI_TouchEnd);

	XISetMask(ximask0, XI_HierarchyChanged);
	XISelectEvents(dpy, wa.root, &ximask, 1);
	XIClearMask(ximask0, XI_HierarchyChanged);
	_signals(sigterm);
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
	int i;
	if (argc == 2 && (!strcmp(argv[1],"-h") || !strcmp(argv[1],"--help"))) {
		fprintf(stdout,"Usage: %s {parameter|-}\n",argv[0]);
		for(i=0; i<MAX_PAR; i++) {
			fprintf(stdout,"%i %2.f %s\n",i,pd[i],ph[i]);
		}
		return 0;
	}
	for (i=0; i<MAX_PAR; i++) pi[i] = pf[i] =  (i<argc-1 && strcmp(argv[i+1],"-")) ? atof(pa[i] = argv[i+1]) :  pd[i];
#endif
	opendpy();
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
		XNextEvent(dpy, &ev);
		switch (ev.type) {
#ifdef XTG
		    case GenericEvent:
			if (ev.xcookie.extension == xiopcode) {
				if (ev.xcookie.evtype == XI_HierarchyChanged) {
					oldShowPtr |= 2;
					continue;
				}
				if (ev.xcookie.evtype < XI_TouchBegin) {
					showPtr = 1;
					continue;
				}
				showPtr = 0;
				if (!tdevs2 || !XGetEventData(dpy, &ev.xcookie)) goto ev;
#undef e
#define e ((XIDeviceEvent*)ev.xcookie.data)
				Touch *to = NULL;
				unsigned short nt = 0, i, g0;
				int j;
				for(i=P; i!=N; TOUCH_INC(i)){
					Touch *t1 = &touch[i];
					if (t1->touchid != e->detail || t1->deviceid != e->deviceid) continue;
					to = t1;
					break;
				}
				if (ev.xcookie.evtype == XI_TouchBegin) {
					if (to) goto evfree;
					g0 = 0;
					if (N!=P && touch[P].g == 99) g0 = 99;
					to = &touch[N];
					TOUCH_INC(N);
					if (N==P) TOUCH_INC(P);
					to->touchid = e->detail;
					to->deviceid = e->deviceid;
					//to->time = e->time;
					to->x = e->root_x;
					to->y = e->root_y;
					to->n = 0;
					to->g = g0;
					to->tail = 0;
					if (pi[p_maxfingers]) {
						nt = 1;
						int n = N;
						TOUCH_DEC(n);
						for (i=P; i!=n; TOUCH_INC(i)) {
							Touch *t1 = &touch[i];
							if (t1->deviceid != to->deviceid) continue;
							t1->n++;
							nt++;
						}
					}
					to->n = nt;
					//if (nt > pi[p_maxfingers]) goto all99;
					goto evfree;
				}
				if (to->g == 99) goto skip;
#ifdef ADAPTIVE2
				Touch *to2=NULL;
#endif
				if (to->n != 1) for (i=P; i!=N; TOUCH_INC(i)) {
					Touch *t1 = &touch[i];
					if (t1->deviceid != to->deviceid) continue;
					if (t1->n != to->n) goto all99;
#ifdef ADAPTIVE2
					if (t1 != to) to2 = t1;
#endif
				}
				int bx = 0, by = 0;
				double xx = e->root_x - to->x, yy = e->root_y - to->y;
				if (xx<0) {xx = -xx; bx = 7;}
				else if (xx>0) bx = 6;
				if (yy<0) {yy = -yy; by = 5;}
				else if (yy>0) by = 4;
#ifdef ADAPTIVE2
#undef res
				double res = pf[p_res];
				if (pi[p_max7]>1 && to2 && to->n == 2) {
					double r = (xx<yy) ? to2->x - to->x : to2->y->to2->y;
					r = r < 0: -r : r;
					r /= 10;
					res = r;
				}
#endif
				if (xx<yy) {
					double s = xx;
					xx = yy;
					yy = s;
					bx = by;
				}
				if (!bx ||
				    xx < res
				    || xx/(yy?:1) < pf[p_xy]
					) {
_bx:
					if (ev.xcookie.evtype != XI_TouchEnd) goto skip;
					if (to->n > 1) goto skip;
					if (to->g) goto skip;
					bx = 1;
					xx = -1;
				}

				to->g = bx;
				if (to->n != 1) for (i=P; i!=N; TOUCH_INC(i)) {
					Touch *t1 = &touch[i];
					if (t1->deviceid != to->deviceid) continue;
					if (t1->g != bx) goto skip;
				}

				switch (bx) {
				    case 1: break;
				    default:
					if (to->n < pi[p_minfingers]) goto skip;
					if (to->n <= pi[p_maxfingers]) {
						if (!pi[p_max7])
							bx += (to->n - pi[p_minfingers]) << 2;
						break;
					}
all99:
					for (i=P; i!=N; TOUCH_INC(i)) {
						Touch *t1 = &touch[i];
						if (t1->deviceid != to->deviceid) continue;
						t1->g = 99;
					}
					goto skip;
				}
				XTestFakeMotionEvent(dpy,screen,to->x+.5,to->y+.5,0);
				XTestFakeButtonEvent(dpy,bx,1,0);
				for (xx-=pf[p_res];xx>=pf[p_res];xx-=pf[p_res]) {
					XTestFakeButtonEvent(dpy,bx,0,0);
					XTestFakeButtonEvent(dpy,bx,1,0);
				}
				XTestFakeMotionEvent(dpy,screen,e->root_x+.5,e->root_y+.5,0);
				XTestFakeButtonEvent(dpy,bx,0,0);
//				XFlush(dpy);
				//to->time = e->time;
				to->x = e->root_x;
				to->y = e->root_y;
				to->tail = xx;
skip:
				if (ev.xcookie.evtype != XI_TouchEnd
					&& !(e->flags & XITouchPendingEnd)
					) goto evfree;
				TOUCH_DEC(N);
				Touch *t1 = &touch[N];
				if (to != t1) *to = *t1;
evfree:
				XFreeEventData(dpy, &ev.xcookie);
				if (oldShowPtr) continue;
			}
			goto ev;
#endif
#ifdef XSS
#undef e
#define e (ev.xclient)
		    case ClientMessage:
			if (e.message_type == aWMState){
				if (e.window==win)
					WMState(&e.data.l[1],e.data.l[0]);
			}
			goto ev;
#endif
#undef e
#define e (ev.xproperty)
		    case PropertyNotify:
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
//			else fprintf(stderr,"ev? %i\n",ev.type);
			break;
		}
	}
}
