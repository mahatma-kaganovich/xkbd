/*
 simple run "<cmdline> <state>" on XScreenSaver events
 examples:
  xssevents xkbd-onoff --xrdb --xss
  xssevents xkbd-onoff --lock --sudo1 /usr/sbin/ya-nrg light 20 save --sudo2 /usr/sbin/ya-nrg restore --xss
  xssevents xkbd-onoff --xrdb --sudo1 /usr/sbin/ya-nrg light 20 cores 0 auto --sudo2 /usr/sbin/ya-nrg restore --xss
*/

#include <unistd.h>
#include <wait.h>
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

int main(int argc, char **argv) {
	XEvent ev;
	char **msg, *m;
	int xssevent, i;
	Display *dpy = XOpenDisplay(NULL);

	if (!dpy || !XScreenSaverQueryExtension(dpy, &xssevent, &i)) return 1;
	xssevent+=ScreenSaverNotify;
	XScreenSaverSelectInput(dpy, XDefaultRootWindow(dpy), ScreenSaverNotifyMask|ScreenSaverCycleMask);
	for(i=1; i<argc; i++) argv[i-1]=argv[i];
	msg = &argv[argc-1];
	while (1) {
		XNextEvent(dpy, &ev);
		if (ev.type == xssevent) {
			switch (((XScreenSaverNotifyEvent*)&ev)->state) {
			case ScreenSaverOff: m="off";break;
			case ScreenSaverOn: m="on";break;
			case ScreenSaverCycle: m="cycle";break;
			case ScreenSaverDisabled: m="disabled";break;
			default: m="unknown";
			}
			*msg=m;
			while(waitpid(-1,NULL,WNOHANG)>0);
			if (!fork()){
				execvp(argv[0], argv);
				return 1;
			}
		}
	}
}
