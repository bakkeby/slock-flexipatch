/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500
#if HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#if HAVE_BSD_AUTH
#include <login_cap.h>
#include <bsd_auth.h>
#endif

#include "arg.h"
#include "util.h"

char *argv0;

enum {
	INIT,
	INPUT,
	FAILED,
	NUMCOLS
};

#include "config.h"

typedef struct {
	int screen;
	Window root, win;
	Pixmap pmap;
	unsigned long colors[NUMCOLS];
} Lock;

static Lock **locks;
static int nscreens;
static Bool running = True;
static Bool failure = False;
static Bool rr;
static int rrevbase;
static int rrerrbase;

static void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

#ifdef __linux__
#include <fcntl.h>
#include <linux/oom.h>

static void
dontkillme(void)
{
	FILE *f;
	const char oomfile[] = "/proc/self/oom_score_adj";

	if (!(f = fopen(oomfile, "w"))) {
		if (errno == ENOENT)
			return;
		die("slock: fopen %s: %s\n", oomfile, strerror(errno));
	}
	fprintf(f, "%d", OOM_SCORE_ADJ_MIN);
	if (fclose(f)) {
		if (errno == EACCES)
			die("slock: unable to disable OOM killer. "
			    "suid or sgid set?\n");
		else
			die("slock: fclose %s: %s\n", oomfile,
			    strerror(errno));
	}
}
#endif

#ifndef HAVE_BSD_AUTH
/* only run as root */
static const char *
getpw(void)
{
	const char *rval;
	struct passwd *pw;

	errno = 0;
	if (!(pw = getpwuid(getuid()))) {
		if (errno)
			die("getpwuid: %s\n", strerror(errno));
		else
			die("cannot retrieve password entry\n");
	}
	rval = pw->pw_passwd;

#if HAVE_SHADOW_H
	if (rval[0] == 'x' && rval[1] == '\0') {
		struct spwd *sp;
		if (!(sp = getspnam(getenv("USER"))))
			die("cannot retrieve shadow entry (make sure to suid or sgid slock)\n");
		rval = sp->sp_pwdp;
	}
#endif

	/* drop privileges */
	if (geteuid() == 0 &&
	    ((getegid() != pw->pw_gid && setgid(pw->pw_gid) < 0) || setuid(pw->pw_uid) < 0))
		die("cannot drop privileges\n");
	return rval;
}
#endif

static void
#ifdef HAVE_BSD_AUTH
readpw(Display *dpy)
#else
readpw(Display *dpy, const char *pws)
#endif
{
	char buf[32], passwd[256], *encrypted;
	int num, screen;
	unsigned int len, color;
	KeySym ksym;
	XEvent ev;
	static int oldc = INIT;

	len = 0;
	running = True;

	/* As "slock" stands for "Simple X display locker", the DPMS settings
	 * had been removed and you can set it with "xset" or some other
	 * utility. This way the user can easily set a customized DPMS
	 * timeout. */
	while (running && !XNextEvent(dpy, &ev)) {
		if (ev.type == KeyPress) {
			explicit_bzero(&buf, sizeof(buf));
			num = XLookupString(&ev.xkey, buf, sizeof(buf), &ksym, 0);
			if (IsKeypadKey(ksym)) {
				if (ksym == XK_KP_Enter)
					ksym = XK_Return;
				else if (ksym >= XK_KP_0 && ksym <= XK_KP_9)
					ksym = (ksym - XK_KP_0) + XK_0;
			}
			if (IsFunctionKey(ksym) ||
			    IsKeypadKey(ksym) ||
			    IsMiscFunctionKey(ksym) ||
			    IsPFKey(ksym) ||
			    IsPrivateKeypadKey(ksym))
				continue;
			switch (ksym) {
			case XK_Return:
				passwd[len] = 0;
#ifdef HAVE_BSD_AUTH
				running = !auth_userokay(getlogin(), NULL, "auth-slock", passwd);
#else
				errno = 0;
				if (!(encrypted = crypt(passwd, pws)))
					fprintf(stderr, "slock: crypt: %s\n", strerror(errno));
				else
					running = !!strcmp(encrypted, pws);
#endif
				if (running) {
					XBell(dpy, 100);
					failure = True;
				}
				explicit_bzero(&passwd, sizeof(passwd));
				len = 0;
				break;
			case XK_Escape:
				explicit_bzero(&passwd, sizeof(passwd));
				len = 0;
				break;
			case XK_BackSpace:
				if (len)
					passwd[len--] = 0;
				break;
			default:
				if (num && !iscntrl((int)buf[0]) && (len + num < sizeof(passwd))) {
					memcpy(passwd + len, buf, num);
					len += num;
				}
				break;
			}
			color = len ? INPUT : (failure || failonclear ? FAILED : INIT);
			if (running && oldc != color) {
				for (screen = 0; screen < nscreens; screen++) {
					XSetWindowBackground(dpy, locks[screen]->win, locks[screen]->colors[color]);
					XClearWindow(dpy, locks[screen]->win);
				}
				oldc = color;
			}
		} else if (rr && ev.type == rrevbase + RRScreenChangeNotify) {
			XRRScreenChangeNotifyEvent *rre = (XRRScreenChangeNotifyEvent*)&ev;
			for (screen = 0; screen < nscreens; screen++) {
				if (locks[screen]->win == rre->window) {
					XResizeWindow(dpy, locks[screen]->win, rre->width, rre->height);
					XClearWindow(dpy, locks[screen]->win);
				}
			}
		} else for (screen = 0; screen < nscreens; screen++)
			XRaiseWindow(dpy, locks[screen]->win);
	}
}

static void
unlockscreen(Display *dpy, Lock *lock)
{
	if(dpy == NULL || lock == NULL)
		return;

	XUngrabPointer(dpy, CurrentTime);
	XFreeColors(dpy, DefaultColormap(dpy, lock->screen), lock->colors, NUMCOLS, 0);
	XFreePixmap(dpy, lock->pmap);
	XDestroyWindow(dpy, lock->win);

	free(lock);
}

static void
cleanup(Display *dpy)
{
	free(locks);
	XCloseDisplay(dpy);
}

static Lock *
lockscreen(Display *dpy, int screen)
{
	char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
	int i;
	Lock *lock;
	XColor color, dummy;
	XSetWindowAttributes wa;
	Cursor invisible;

	if (!running || dpy == NULL || screen < 0 || !(lock = malloc(sizeof(Lock))))
		return NULL;

	lock->screen = screen;
	lock->root = RootWindow(dpy, lock->screen);

	for (i = 0; i < NUMCOLS; i++) {
		XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen), colorname[i], &color, &dummy);
		lock->colors[i] = color.pixel;
	}

	/* init */
	wa.override_redirect = 1;
	wa.background_pixel = lock->colors[INIT];
	lock->win = XCreateWindow(dpy, lock->root, 0, 0, DisplayWidth(dpy, lock->screen), DisplayHeight(dpy, lock->screen),
	                          0, DefaultDepth(dpy, lock->screen), CopyFromParent,
	                          DefaultVisual(dpy, lock->screen), CWOverrideRedirect | CWBackPixel, &wa);
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap, &color, &color, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);

	/* Try to grab mouse pointer *and* keyboard, else fail the lock */
	if (XGrabPointer(dpy, lock->root, False, ButtonPressMask |
	    ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync,
	    None, invisible, CurrentTime) != GrabSuccess) {
		fprintf(stderr, "slock: unable to grab mouse pointer for screen %d\n", screen);
		running = 0;
		unlockscreen(dpy, lock);
		return NULL;
	}

	if (XGrabKeyboard(dpy, lock->root, True, GrabModeAsync, GrabModeAsync,
	    CurrentTime) != GrabSuccess) {
		fprintf(stderr, "slock: unable to grab keyboard for screen %d\n", screen);
		running = 0;
		unlockscreen(dpy, lock);
		return NULL;
	}

	XMapRaised(dpy, lock->win);
	if (rr)
		XRRSelectInput(dpy, lock->win, RRScreenChangeNotifyMask);

	XSelectInput(dpy, lock->root, SubstructureNotifyMask);
	return lock;
}

static void
usage(void)
{
	die("usage: slock [-v] [cmd [arg ...]]\n");
}

int
main(int argc, char **argv) {
#ifndef HAVE_BSD_AUTH
	const char *pws;
#endif
	Display *dpy;
	int s, nlocks;

	ARGBEGIN {
	case 'v':
		fprintf(stderr, "slock-"VERSION"\n");
		return 0;
	default:
		usage();
	} ARGEND

#ifdef __linux__
	dontkillme();
#endif

	/* Check if the current user has a password entry */
	errno = 0;
	if (!getpwuid(getuid())) {
		if (errno == 0)
			die("slock: no password entry for current user\n");
		else
			die("slock: getpwuid: %s\n", strerror(errno));
	}

#ifndef HAVE_BSD_AUTH
	pws = getpw();
	if (strlen(pws) < 2)
		die("slock: failed to get user password hash.\n");
#endif

	if (!(dpy = XOpenDisplay(NULL)))
		die("slock: cannot open display\n");

	/* check for Xrandr support */
	rr = XRRQueryExtension(dpy, &rrevbase, &rrerrbase);

	/* get number of screens in display "dpy" and blank them */
	nscreens = ScreenCount(dpy);
	if (!(locks = malloc(sizeof(Lock *) * nscreens))) {
		XCloseDisplay(dpy);
		die("slock: out of memory\n");
	}
	for (nlocks = 0, s = 0; s < nscreens; s++) {
		if ((locks[s] = lockscreen(dpy, s)) != NULL)
			nlocks++;
	}
	XSync(dpy, 0);

	/* did we actually manage to lock anything? */
	if (nlocks == 0) {
		/* nothing to protect */
		cleanup(dpy);
		return 1;
	}

	/* run post-lock command */
	if (argc > 0) {
		switch (fork()) {
		case -1:
			cleanup(dpy);
			die("slock: fork failed: %s\n", strerror(errno));
		case 0:
			if (close(ConnectionNumber(dpy)) < 0)
				die("slock: close: %s\n", strerror(errno));
			execvp(argv[0], argv);
			fprintf(stderr, "slock: execvp %s: %s\n", argv[0],
			        strerror(errno));
			_exit(1);
		}
	}

	/* everything is now blank. Wait for the correct password */
#ifdef HAVE_BSD_AUTH
	readpw(dpy);
#else
	readpw(dpy, pws);
#endif

	/* password ok, unlock everything and quit */
	for (s = 0; s < nscreens; s++)
		unlockscreen(dpy, locks[s]);

	cleanup(dpy);

	return 0;
}
