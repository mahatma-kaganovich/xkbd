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

#include <X11/Xproto.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>

#define grp_t CARD32
#define NO_GRP 99

#if Window != CARD32
#include <string.h>
#endif

Display *dpy;
Window win, win1;
XWindowAttributes wa;
Atom aActWin, aKbdGrp, aXkbRules;
grp_t grp, grp1;
unsigned char *rul, *rul2;
unsigned long rulLen;
int xkbEventType, xkbError, n1;
XEvent ev;

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

Bool getProp(Window w, Atom prop, Atom type, void *res, int size){
	Atom t;
	int f;
	unsigned long n=0,b;
	unsigned char *ret = NULL;

//	XFlush(dpy);
//	XSync(dpy,False);
	if (XGetWindowProperty(dpy,w,prop,0,1,False,type,&t,&f,&n,&b,&ret)==Success && ret) {
#if Window == CARD32
		if (n>0 && f==32) {
			*(CARD32*)res = *(CARD32*)ret;
#else
		if (n>0 && (f>>3) == size) {
			memcpy(res,ret,size);
#endif
			XFree(ret);
			return True;
		}
		XFree(ret);
	}
	return False;
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
		fprintf(stdout,"%lu\n",(unsigned long)grp1);
	}
	fflush(stdout);
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
	while (grp1 != grp && XkbLockGroup(dpy, XkbUseCoreKbd, grp1)!=Success && grp1) {
		XDeleteProperty(dpy,win,aKbdGrp);
		grp1 = 0;
	}
}

void setWinGrp(){
	printGrp();
	if (win!=wa.root
		// optimization, may be omitted
		&& (getProp(win,aKbdGrp,XA_CARDINAL,&grp,sizeof(grp)) ? grp != grp1 : grp1)
	    )
		XChangeProperty(dpy,win,aKbdGrp,XA_CARDINAL,32,PropModeReplace,(unsigned char*) &grp1,1);
	grp = grp1;
}

void init(){
	int reason_rtrn, xkbmjr = XkbMajorVersion, xkbmnr = XkbMinorVersion;

	dpy = XkbOpenDisplay(NULL,&xkbEventType,&xkbError,&xkbmjr,&xkbmnr,&reason_rtrn);
	XGetWindowAttributes(dpy, DefaultRootWindow(dpy), &wa);
	aActWin = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	aKbdGrp = XInternAtom(dpy, "_KBD_ACTIVE_GROUP", False);
	aXkbRules = XInternAtom(dpy, "_XKB_RULES_NAMES", False);
	XkbSelectEvents(dpy,XkbUseCoreKbd,XkbAllEventsMask,
		XkbStateNotifyMask
//		|XkbNewKeyboardNotifyMask
		);
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
	rul = NULL;
}

int main(){
	init();
	printGrp();
	while (1) {
		if (win1 != win) getWinGrp();
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
			 if (e.atom==aActWin) {
				if (e.window==wa.root) win1 = 0;
			 } else if (e.atom==aKbdGrp) {
				if (e.window==win) win = 0;
			 } else if (e.atom==aXkbRules) {
				if (e.window==wa.root) {
					rul2 = NULL;
					printGrp();
				}
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
					setWinGrp();
					break;
				    //case XkbNewKeyboardNotify:
					//break;
				}
			}
			break;
		}
	}
}
