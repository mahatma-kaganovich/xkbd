/*
	xkbdd v1.8 - per-window keyboard layout switcher [+ XSS suspend].
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
	May be +gestures in future.

	(C) 2019-2020 Denis Kaganovich, Anarchy license
*/
#ifndef NO_ALL
#define XSS
#define XTG
#endif

// listen XI_Touch* events directly from device cannot twice:
// second run can kill us (BadAccess).
// while I believe this is bug and this code is better, keep.
//#define TOUCH_EVENTS_SLAVE

// hiding absolute pointers is bad idea, as pen have inertia & buttons.
// but keep it here
//#define HIDE_ABS

#include <stdio.h>

#include <X11/Xproto.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>

#ifdef XSS
#include <X11/extensions/scrnsaver.h>
#endif

#ifdef XTG
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
//#include <string.h>
#endif

#define NO_GRP 99

static Display *dpy;
static Window win, win1;
static int screen;
static XWindowAttributes wa;
static Atom aActWin, aKbdGrp, aXkbRules;
#ifdef XSS
static Atom aWMState, aFullScreen;
static Bool noXSS, noXSS1;
#endif
static CARD32 grp, grp1;
static unsigned char *rul, *rul2;
static unsigned long rulLen, n;
static int xkbEventType, xkbError, n1, revert;
static XEvent ev;
static unsigned char *ret;
#ifdef XTG
static int xiopcode, ndevs2;
static XDevice *dev2;
static int ndevs2, lastid = 0;
static XIDeviceInfo *info2;
#define MASK_LEN = XIMaskLen(XI_LASTEVENT)
typedef unsigned char xiMask[XIMaskLen(XI_LASTEVENT)];
static xiMask ximask0 = {};
static xiMask ximaskButton = {};
static xiMask ximaskTouch = {};
static XIEventMask ximask = { .deviceid = XIAllDevices, .mask_len =  XIMaskLen(XI_LASTEVENT), .mask = ximaskButton };
#endif

static void opendpy() {
	int reason_rtrn, xkbmjr = XkbMajorVersion, xkbmnr = XkbMinorVersion;
	dpy = XkbOpenDisplay(NULL,&xkbEventType,&xkbError,&xkbmjr,&xkbmnr,&reason_rtrn);
#ifdef XTG
	int xievent = 0, xierror, xi = 0;
	xiopcode = 0;
	dev2 = NULL;
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
static Bool _isTouch(XIDeviceInfo *d2) {
	int i;
	for (i=0; i<d2->num_classes; i++)
		if (d2->classes[i]->type == XITouchClass)
			return 1;
	return 0;
}

static Bool _isAbs(XIDeviceInfo *d2) {
	int i;
	for (i=0; i<d2->num_classes; i++)
		if (d2->classes[i]->type == XIValuatorClass
		    && ((XIValuatorClassInfo*)d2->classes[i])->mode==Absolute)
			return 1;
	return 0;
}

static XIDeviceInfo *_getDevice(int id){
	int i;
	XIDeviceInfo *d2 = info2;
	for(i=0; i<ndevs2; i++) {
		if (d2->deviceid == id) return d2;
		d2++;
	}
	return NULL;
}

static short showPtr = 1, oldShowPtr = 0;
static void setShowCursor(){
	int i;
	XIDeviceInfo *d2 = info2;
	Bool show,t;
	for(i=0; i<ndevs2; i++) {
		if (d2->enabled) switch (d2->use) {
		    case XIFloatingSlave:
		    case XISlavePointer:
			t = _isTouch(d2);
#ifdef HIDE_ABS
			show = !(t || _isAbs(d2));
#else
			show = !t;
#endif
#ifdef TOUCH_EVENTS_SLAVE
			ximask.mask = (unsigned char *)((show^showPtr)?t?&ximaskTouch:&ximaskButton:&ximask0);
#else
			ximask.mask = (unsigned char *)((show^showPtr)?&ximaskButton:&ximask0);
#endif
			ximask.deviceid = d2->deviceid;
			XISelectEvents(dpy, wa.root, &ximask, 1);
		}
		d2++;
	}
#undef e
#define e ((XIDeviceEvent*)ev.xcookie.data)
	if (showPtr != oldShowPtr) {
//	if (e) {
//		XWarpPointer(dpy, None, wa.root, 0, 0, 0, 0, e->root_x+0.5, e->root_y+0.5);
		if (showPtr) XFixesShowCursor(dpy, wa.root);
		else XFixesHideCursor(dpy, wa.root);
		oldShowPtr = showPtr;
//	}
	}
	XFlush(dpy);
}

void getHierarchy(){
	if(info2) XIFreeDeviceInfo(info2);
	info2 = XIQueryDevice(dpy, XIAllDevices, &ndevs2);
//	ev.xcookie.data = NULL;
	setShowCursor();
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

static int (*oldxerrh) (Display *, XErrorEvent *);
static int xerrh(Display *dpy, XErrorEvent *err){
	if (err->error_code!=BadWindow || !XFlush(dpy) || !XSync(dpy,False)) oldxerrh(dpy,err);
	//fprintf(stderr,"BadWindow ev=%x %s\n",ev.type,ev.type==28?XGetAtomName(dpy,ev.xproperty.atom):"");
	win1 = None;
	return 0;
}

static void init(){
	int evmask = PropertyChangeMask;

	opendpy();
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
	XISetMask(ximask.mask, XI_HierarchyChanged); // only AllDevices!
	XISelectEvents(dpy, wa.root, &ximask, 1);
	XIClearMask(ximask.mask, XI_HierarchyChanged);

	XISetMask(ximaskButton, XI_ButtonPress);
	XISetMask(ximaskButton, XI_ButtonRelease);
	XISetMask(ximaskButton, XI_Motion);

	XISetMask(ximaskTouch, XI_TouchBegin);
	XISetMask(ximaskTouch, XI_TouchUpdate);
	XISetMask(ximaskTouch, XI_TouchEnd);
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
int main(){
	init();
	printGrp();
	getPropWin1();
#ifdef XTG
	XFixesHideCursor(dpy, wa.root);
	getHierarchy();
#endif
	while (1) {
		if (win1 != win) getWinGrp();
		else if (grp1 != grp) setWinGrp();
		if (win1 != win) continue; // error handled
		XNextEvent(dpy, &ev);
		switch (ev.type) {
#ifdef XTG
		    case GenericEvent:
			if (ev.xcookie.extension == xiopcode) {
				XIDeviceInfo *d2;
				switch (ev.xcookie.evtype) {
				    case XI_HierarchyChanged:
					getHierarchy();
					continue;
				}
				if (!XGetEventData(dpy, &ev.xcookie)) continue;
				if (e->deviceid != e->sourceid) goto evfree;
				switch (ev.xcookie.evtype) {
#undef e
#define e ((XIDeviceEvent*)ev.xcookie.data)
#ifdef TOUCH_EVENTS_SLAVE
				    case XI_TouchEnd:
				    case XI_TouchUpdate:
				    case XI_TouchBegin:
					showPtr = 0;
					break;
				    default:
#ifdef HIDE_ABS
					showPtr = (d2=_getDevice(e->sourceid)) && !_isAbs(d2);
#else
					showPtr = 1;
#endif
#else
				    default:
#ifdef HIDE_ABS
					showPtr = (d2=_getDevice(e->sourceid)) && !(_isTouch(d2) || _isAbs(d2));
#else
					showPtr = (d2=_getDevice(e->sourceid)) && !_isTouch(d2);
#endif
#endif
					break;
				}
				if (e->sourceid != lastid) {
					lastid = e->sourceid;
					if (showPtr != oldShowPtr) setShowCursor();
				}
evfree:
				XFreeEventData(dpy, &ev.xcookie);
			}
			break;
#endif
#ifdef XSS
#undef e
#define e (ev.xclient)
		    case ClientMessage:
			if (e.message_type == aWMState){
				if (e.window==win)
					WMState(&e.data.l[1],e.data.l[0]);
			}
			break;
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
