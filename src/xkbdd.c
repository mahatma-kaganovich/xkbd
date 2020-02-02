/*
	xkbdd v1.5 - per-window keyboard layout switcher [+ XSS suspend].
	Common principles looked up from kbdd http://github.com/qnikst/kbdd
	- but rewrite from scratch.

	I found some problems in kbdd (for example - startup WM detection),
	CPU usage and strange random bugs...

	But looking in code - I found this is terrible, overcoded and required
	to be much simpler.

	I use window properties for state tracking, unify WM model
	and print layout name to STDIN per line to use with tint2.

	Default layout always 0: respect single point = layout config order.


	XSS suspend (-DXSS):
	Disable XScreenSaver if fullscreen window focused.
	Added to xkbdd as massive code utilization.


	(C) 2019-2020 Denis Kaganovich, Anarchy license
*/

#include <stdio.h>

#include <X11/Xproto.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>

#ifdef XSS
#include <X11/extensions/scrnsaver.h>
#endif

#define NO_GRP 99

Display *dpy;
Window win, win1;
XWindowAttributes wa;
Atom aActWin, aKbdGrp, aXkbRules;
#ifdef XSS
Atom aWMState, aFullScreen, aCLStacking;
BOOL noXSS, noXSS1;
#endif
CARD32 grp, grp1;
unsigned char *rul, *rul2;
unsigned long rulLen, n;
int xkbEventType, xkbError, n1, revert;
XEvent ev;
unsigned char *ret;


Bool getRules(){
	Atom t;
	int f, i;
	unsigned long b;

	XFree(rul);
	rul = NULL;
	rulLen = 0;
	n1 = 0;
	if (XGetWindowProperty(dpy,wa.root,aXkbRules,0,256,False,XA_STRING,&t,&f,&rulLen,&b,&rul)==Success && rul) {
		rul2 = rul;
		if (rulLen>1 && t==XA_STRING) return 1;
		XFree(rul);
	}
	rul2 = NULL;
	return 0;
}

int getProp(Window w, Atom prop, Atom type, int size){
	Atom t;
	int f;
	unsigned long b;

	if (ret) XFree(ret);
	ret = NULL;
//	XFlush(dpy);
//	XSync(dpy,False);
	if (!(XGetWindowProperty(dpy,w,prop,0,1024,False,type,&t,&f,&n,&b,&ret)==Success && f>>3 == size && ret))
		n=0;
	return n;
}

void printGrp(){
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
void getWMState(){
	int i;
	noXSS1 = False;
	if (getProp(win,aWMState,XA_ATOM,sizeof(Atom))) {
		for(i=0; i<n; i++) {
			if (((Atom*)ret)[i] == aFullScreen) {
				noXSS1 = True;
				break;
			}
		}
	}
	if (noXSS1!=noXSS) XScreenSaverSuspend(dpy,noXSS=noXSS1);
}
#endif

void getWinGrp(){
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
	getWMState();
#endif
}

void setWinGrp(){
	printGrp();
	if (win!=wa.root)
		XChangeProperty(dpy,win,aKbdGrp,XA_CARDINAL,32,PropModeReplace,(unsigned char*) &grp1,1);
	grp = grp1;
}

void getPropWin1(){
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

void init(){
	int reason_rtrn, xkbmjr = XkbMajorVersion, xkbmnr = XkbMinorVersion,
		evmask = PropertyChangeMask;

	dpy = XkbOpenDisplay(NULL,&xkbEventType,&xkbError,&xkbmjr,&xkbmnr,&reason_rtrn);
	XGetWindowAttributes(dpy, DefaultRootWindow(dpy), &wa);
	aActWin = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	aKbdGrp = XInternAtom(dpy, "_KBD_ACTIVE_GROUP", False);
	aXkbRules = XInternAtom(dpy, "_XKB_RULES_NAMES", False);
#ifdef XSS
	aWMState = XInternAtom(dpy, "_NET_WM_STATE", False);
	aFullScreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	aCLStacking = XInternAtom(dpy, "_NET_CLIENT_LIST_STACKING", False);
	noXSS = False;
	evmask |= SubstructureNotifyMask;
#endif
	XkbSelectEvents(dpy,XkbUseCoreKbd,XkbAllEventsMask,
		XkbStateNotifyMask
//		|XkbNewKeyboardNotifyMask
		);
	XkbSelectEventDetails(dpy,XkbUseCoreKbd,XkbStateNotify,
		XkbAllStateComponentsMask,XkbGroupStateMask);
	XSelectInput(dpy, wa.root, evmask);
	oldxerrh = XSetErrorHandler(xerrh);
	win = wa.root;
	win1 = None;
	grp = NO_GRP;
	grp1 = 0;
	rul = NULL;
	ret = NULL;
}

int main(){
	init();
	printGrp();
	while (1) {
		if (win1 != win) getWinGrp();
		else if (grp1 != grp) setWinGrp();
		if (win1 != win) continue; // error handled
		XNextEvent(dpy, &ev);
		switch (ev.type) {
#ifdef XSS
#undef e
#define e (ev.xclient)
		    case ClientMessage:
			if (e.message_type == aWMState){
				getPropWin1();
				if (e.window == win1 && win1 == win)
					getWMState();
			}
			break;
#endif
#undef e
#define e (ev.xproperty)
		    case PropertyNotify:
			//if (e.window!=wa.root) break;
			if (e.atom==aActWin) {
				win1 = None;
				getPropWin1();
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
			break;
		}
	}
}
