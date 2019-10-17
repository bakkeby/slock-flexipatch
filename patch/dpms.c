#include <X11/extensions/dpms.h>

static void
monitorreset(Display* dpy, CARD16 standby, CARD16 suspend, CARD16 off)
{
	DPMSSetTimeouts(dpy, standby, suspend, off);
	DPMSForceLevel(dpy, DPMSModeOn);
	XFlush(dpy);
}