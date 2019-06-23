#include "libXkbd.h"

Xkbd*
xkbd_realize(Display *display,
	     Drawable dest,
	     char *conf_file,
	     char *font_name,
	     int x,
	     int y,
	     int width,
	     int height,
	     int flags)
{
   XkbStateRec Xkb_state;
   Xkbd *xkbd = malloc(sizeof(Xkbd));

   xkbd->kb = kb_new(dest, display, x, y,
		     width, height, conf_file,
		     font_name, flags  );
   xkbd->active_but = NULL;

    /* we can read current state or reset (& pass to event)
       reset solve some errors (XkbGetState stuck failed on some combination)
       even if we not use XkbGetState, keeping X server happy is good */

//   if (Xkb_sync)
//	XkbGetState(display, XkbUseCoreKbd, Xkb_state);
//   if(Xkb_state)
//	xkbd_sync_state(xkbd,Xkb_state->mods,Xkb_state->locked_mods,Xkb_state->group);

   /* to event -> working starting here */

   kb_size(xkbd->kb);
   return xkbd;
}

void
xkbd_resize(Xkbd *xkbd, int width, int height)
{
   xkbd->kb->vbox->act_width = width;
   xkbd->kb->vbox->act_height = height;
   kb_size(xkbd->kb);
   kb_render(xkbd->kb);
   kb_paint(xkbd->kb);
}

void
xkbd_move(Xkbd *kb, int x, int y)
{
   ;
}

void
xkbd_repaint(Xkbd *xkbd)
{
//   kb_size(xkbd->kb);
   kb_render(xkbd->kb);
   kb_paint(xkbd->kb);
}

void xkbd_process(Xkbd *xkbd, int type, int x, int y, uint32_t ptr, int dev, Time time)
{
   xkbd->active_but = kb_handle_events(xkbd->kb, type, x, y, ptr, dev, time);
}

Bool xkbd_process_repeats(Xkbd *xkbd)
{
   return kb_do_repeat(xkbd->kb, xkbd->active_but);
}

int xkbd_get_width(Xkbd *xkbd)
{
   return xkbd->kb->vbox->act_width;
}

int xkbd_get_height(Xkbd *xkbd)
{
   return xkbd->kb->vbox->act_height;
}

void
xkbd_destroy(Xkbd *kb)
{
   ;

}

unsigned int _set_state(unsigned int *s, unsigned int new)
{
	unsigned int old = *s & KB_STATE_KNOWN;
	new &= KB_STATE_KNOWN;
	*s = (*s & ~KB_STATE_KNOWN)|new;
	return old^new;
}

unsigned int xkbd_sync_state(Xkbd *xkbd, unsigned int mods, unsigned int locked_mods, int group)
{
	unsigned int ch=0;
	int i=0;
	Display *dpy = xkbd->kb->display;

//	locked_mods &= STATE(KBIT_CAPS) | ~KB_STATE_KNOWN;

	if (((mods|locked_mods) ^ ((xkbd->kb->state|xkbd->kb->state_locked)) && KB_STATE_KNOWN))
		ch|=_set_state(&xkbd->kb->state, mods)|_set_state(&xkbd->kb->state_locked, locked_mods);
	if (group!=xkbd->kb->group){
		kb_switch_layout(xkbd->kb,group,0);
		ch=0;
	}
//	XkbGetIndicatorState(dpy,XkbUseCoreKbd,&i);
	return ch;
}

