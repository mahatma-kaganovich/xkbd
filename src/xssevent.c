/*
 simple run "<cmdline> --xss <state>" on XScreenSaver events
 examples:
  xssevents xkbd-onoff --xrdb
  xssevents xkbd-onoff --xrdb --sudo1 /usr/sbin/ya-nrg light 20 cores 0 save --sudo2 /usr/sbin/ya-nrg restore
*/

#include <stdlib.h>
#include <unistd.h>
#include <wait.h>
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

int main(int argc, char **argv) {
	XEvent ev;
	char **msg;
	char **a;
	int xssevent, xsserr, xss, i;
	Display *dpy = XOpenDisplay(NULL);

	if (!dpy || !XScreenSaverQueryExtension(dpy, &xssevent, &xsserr)) return 1;
	XScreenSaverSelectInput(dpy, XDefaultRootWindow(dpy), ScreenSaverNotifyMask|ScreenSaverCycleMask);
	for(i=1; argv[i]; i++);
	a=malloc(sizeof(char*)*(i+1));
	for(i=1; argv[i]; i++) a[i-1]=argv[i];
	a[i-1]="--xss";
	a[i+1]=NULL;
	msg=&a[i];
	while (1) {
		XNextEvent(dpy, &ev);
		if (ev.type == xssevent+ScreenSaverNotify) {
			switch (((XScreenSaverNotifyEvent*)&ev)->state) {
			case ScreenSaverOff: *msg="off";break;
			case ScreenSaverOn: *msg="on";break;
			case ScreenSaverCycle: *msg="cycle";break;
			case ScreenSaverDisabled: *msg="disabled";break;
			default: *msg="unknown";
			}
			while(waitpid(-1,NULL,WNOHANG)>0);
			if (!fork()){
				execvp(a[0], a);
				return 1;
			}
		}
	}
}
