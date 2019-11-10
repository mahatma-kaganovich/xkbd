/*
	xkbdd v1.2 - per-window keyboard layout switcher.
	Common principles looked up from kbdd http://github.com/qnikst/kbdd
	- but rewrite from scratch.

	I found some problems in kbdd (for example - startup WM detection),
	CPU usage and strange random bugs...

	But looking in code - I found this is terrible, overcoded and required
	to be much simpler.

	I use window properties for state tracking, unify WM model
	and print layout name to STDIN per line to use with tint2.

	Default layout always 0: respect single point = layout config order.

	(C) 2019 Denis Kaganovich, Anarchy license
*/

#include <stdio.h>
#include <string.h>

#include <X11/Xproto.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>

#define grp_t CARD32
#define NO_GRP 99

Display *dpy;
Window win, win1;
XWindowAttributes wa;
Atom aActWin, aKbdGrp, aXkbRules;
grp_t grp, grp1;
int xkbEventType, xkbError;
XEvent ev;

Bool getProp(Window w, Atom prop, Atom type, void *res, int size){
	Atom t;
	int f;
	unsigned long n=0,b;
	unsigned char *ret = NULL;

//	XFlush(dpy);
//	XSync(dpy,False);
	if (XGetWindowProperty(dpy,w,prop,0,1,False,type,&t,&f,&n,&b,&ret)==Success) {
		if (n>0 && t==type) {
			memcpy(res,ret,size);
			XFree(ret);
			return True;
		}
		XFree(ret);
	}
	return False;
}

void printGrp(){
	Atom t;
	int f, i, n1=0, n2=0;
	unsigned long n=0,b;
	unsigned char *ret = NULL, *s, *p;

	if (XGetWindowProperty(dpy,wa.root,aXkbRules,0,256,False,XA_STRING,&t,&f,&n,&b,&ret)==Success) {
		if (n>0 && t==XA_STRING && ret) {
			p = s = ret;
			for (i=0; i<n; i++) {
				switch (*(s++)) {
				case '\0':
					if ((n2==grp1 && n1>1) || n1>2) break;
					n1++;
					n2=0;
					p=s;
					continue;
				case ',':
					if (n2==grp1 && n1>1) break;
					n2++;
					p=s;
					continue;
				default:
					continue;;
				}
				break;
			}
		}
		XFree(ret);
	}
	if (n1==2 && n2==grp1) {
		*(s-1)=0;
		fprintf(stdout,"%s\n",p);
	} else {
		fprintf(stdout,"%lu\n",(unsigned long)grp1);
	}
	fflush(stdout);
}

void syncGrp(){
	//XFlush(dpy);
	//XSync(dpy,False);
	grp = grp1;
}

void getWin(){
	int revert;
	//win1 = 0;
	if ((!getProp(wa.root,aActWin,XA_WINDOW,&win1,sizeof(win1)) || !win1) &&
	    (XGetInputFocus(dpy, &win1, &revert)!=Success || !win1))
		win1 = wa.root;
}

void getWinGrp(){
	if (!win1) getWin();
	win = win1;
	grp1 = 0;
	if (win!=wa.root)
		getProp(win,aKbdGrp,XA_CARDINAL,&grp1,sizeof(grp1));
	if (grp1 != grp) {
		XkbLockGroup(dpy, XkbUseCoreKbd, grp1);
		printGrp();
		syncGrp();
	}
}

void setWinGrp(){
	printGrp();
	if (win!=wa.root
		// optimization, may be omitted
		&& (getProp(win,aKbdGrp,XA_CARDINAL,&grp,sizeof(grp)) ? grp != grp1 : grp1)
	    )
		XChangeProperty(dpy,win,aKbdGrp,XA_CARDINAL,32,PropModeReplace,(unsigned char*) &grp1,1);
	syncGrp();
}

void init(){
	int reason_rtrn, xkbmjr = XkbMajorVersion, xkbmnr = XkbMinorVersion;

	dpy = XkbOpenDisplay(NULL,&xkbEventType,&xkbError,&xkbmjr,&xkbmnr,&reason_rtrn);
	XGetWindowAttributes(dpy, DefaultRootWindow(dpy), &wa);
	aActWin = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	aKbdGrp = XInternAtom(dpy, "_KBD_ACTIVE_GROUP", False);
	aXkbRules = XInternAtom(dpy, "_XKB_RULES_NAMES", False);
	XkbSelectEvents(dpy,XkbUseCoreKbd,XkbAllEventsMask,XkbStateNotifyMask|XkbNewKeyboardNotifyMask);
	XkbSelectEventDetails(dpy,XkbUseCoreKbd,XkbStateNotify,
		XkbAllStateComponentsMask,XkbGroupStateMask);
	XSelectInput(dpy, wa.root,
		PropertyChangeMask|
		FocusChangeMask|
		wa.your_event_mask);
	win = wa.root;
	win1 = 0;
	grp = NO_GRP;
	grp1 = 0;
}

int main(){
	init();
	while (1) {
		if (win1 != win) getWinGrp();
		else if (grp1 != grp) setWinGrp();
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		    case FocusOut:
			//win1 = wa.root;
			break;
		    case FocusIn:
			win1 = ev.xfocus.window;
			break;
#undef e
#define e (ev.xproperty)
		    case PropertyNotify:
			if (e.window==wa.root && e.atom==aActWin) win1 = 0;
			else if (e.window==win && e.atom==aKbdGrp) {
				if (e.state==PropertyDelete) grp1 = 0;
				else win = 0;
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
				    case XkbNewKeyboardNotify:
					grp = NO_GRP;
					break;
				}
			}
			break;
		}
	}
}
