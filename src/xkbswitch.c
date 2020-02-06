/* simple switch layout */

#include <X11/Xlib.h>
#include <X11/XKBlib.h>

int main(){
	Display *dpy = XOpenDisplay(NULL);
	XkbStateRec s;

	XkbGetState(dpy,XkbUseCoreKbd,&s);
	XkbLockGroup(dpy,XkbUseCoreKbd,s.group+1);
	XCloseDisplay(dpy);
	return 0;
}
