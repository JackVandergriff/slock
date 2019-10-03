#ifndef cover
#define cover

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <sys/time.h>

#define PL_SEG 7
#define SLATE 0x24333AA0
#define PL_COUNTDOWN 100

struct lock_properties {
	unsigned int lwidth;
	unsigned int lheight;
	int width;
	int height;
	cairo_surface_t* isurf;
};

enum PL_DATA {PL_CAIRO, PL_LOCK, PL_DISPLAY, PL_WINDOW};

enum PL_CONTROL {
	PL_DRAW			= 1 << 0, // Draw without clearing
	PL_SET_ALPHA	= 1 << 1, // data -> alpha (lowest btye)
	PL_SET_COLOR	= 1 << 2, // data -> color (4 byte argb)
	PL_SET_KEYBOARD = 1 << 3, // data -> draw segment (0 - PL_SEG)
	PL_INIT			= 1 << 4, // initialize with PL_DATA and pointer
	PL_CLEAR		= 1 << 5, // clears window
	PL_CONFIGURE	= 1 << 6, // XEvent* -> width and height
	PL_FREE			= 1 << 7, // Frees lock, required at end
	PL_REDRAW		= 0x21	  // Draw and clear
};

void pl_control(int, int, void*);
void alrm(int);
Window getLockCover(Display*);
int LockEventHandler(Display*, XEvent);
void TimerHandler(int);

void pl_control(int control, int data, void* pointer) {
	static cairo_t* cr;
	static struct lock_properties* lp;
	static Display* dpy;
	static Window win;
	static int color, key;
	if (control & PL_INIT) {
		assert(pointer != NULL);
		if (data == PL_LOCK)
			lp = (struct lock_properties*) pointer;
		if (data == PL_CAIRO)
			cr = (cairo_t*) pointer;
		if (data == PL_DISPLAY)
			dpy = (Display*) pointer;
		if (data == PL_WINDOW)
			win = * ((Window*) pointer);
	} if (control & PL_CONFIGURE) {
		XEvent ev = * ((XEvent*) pointer);
		if (lp->width != ev.xconfigure.width ||
			lp->height != ev.xconfigure.height) {
			lp->width = ev.xconfigure.width;
			lp->height = ev.xconfigure.height;
		}
	} if (control & PL_CLEAR) {
		XClearWindow(dpy, win);
	} if (control & PL_SET_COLOR) {
		cairo_set_source_rgba(cr,
				(double) ((data >> 24) & 0xFF) / 255,
				(double) ((data >> 16) & 0xFF) / 255,
				(double) ((data >> 8 ) & 0xFF) / 255,
				(double) ((data >> 0 ) & 0xFF) / 255);
		if (pointer != NULL)
			*((int*) pointer) = color;
		color = data;
	} if (control & PL_SET_ALPHA) {
		cairo_set_source_rgba(cr,
				(double) ((color >> 24) & 0xFF) / 255,
				(double) ((color >> 16) & 0xFF) / 255,
				(double) ((color >> 8 ) & 0xFF) / 255,
				(double) ((data  >> 0 ) & 0xFF) / 255);
		if (pointer != NULL)
			*((int*) pointer) = color & 0xFF;
		color = (color & 0xFFFFFF00) | (data & 0xFF);
	} if (control & PL_DRAW) {
		assert(cr != NULL);
		assert(lp != NULL);
		cairo_mask_surface(	cr, lp->isurf,
							(lp->width-lp->lwidth)/2,
							(lp->height-lp->lheight)/2);
	} if (control & PL_SET_KEYBOARD) {
		cairo_set_line_width (cr, 15.0);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		if (data >= 0) {
			cairo_arc(	cr,
						lp->width/2,
						lp->height/2,
						lp->lheight/2+30,
						((double) data    ) * 2.0 * M_PI / PL_SEG,
						((double) data + 1) * 2.0 * M_PI / PL_SEG);
			key = data;
		} else {
			cairo_arc(	cr,
						lp->width/2,
						lp->height/2,
						lp->lheight/2+30,
						((double) key    ) * 2.0 * M_PI / PL_SEG,
						((double) key + 1) * 2.0 * M_PI / PL_SEG);
		}
		cairo_stroke(cr);
	} if (control & PL_FREE) {
		free(lp);
	}
}

void TimerHandler(int signum) {
	static int count;
	if (signum == -1) {
		count = PL_COUNTDOWN;
	} else if (count > 0) {
		pl_control(PL_REDRAW, 0, NULL);
		pl_control(PL_SET_ALPHA, count * 0xA0 / PL_COUNTDOWN, NULL);
		pl_control(PL_SET_KEYBOARD, -1, NULL);
		pl_control(PL_SET_COLOR, SLATE, NULL);
		count--;
	}
}

Window getLockCover(Display* dpy){
	struct sigaction sa;
	struct itimerval timer;
	Window win;
	struct lock_properties* lp;
	XClassHint* classHint;
	Colormap cmp;

	cairo_surface_t *xsurf, *isurf;
	cairo_t* cairo;

	Window root = DefaultRootWindow(dpy);
	int screenNum = DefaultScreen(dpy);

	lp = malloc(sizeof(struct lock_properties));
	lp->width  = DisplayWidth (dpy, screenNum);
	lp->height = DisplayHeight(dpy, screenNum) + 1;

	XVisualInfo vinfo;
	XMatchVisualInfo(dpy, screenNum, 32, TrueColor, &vinfo);
	cmp = XCreateColormap(dpy, root, vinfo.visual, AllocNone);

	XSetWindowAttributes attr;
	attr.colormap = cmp;
	attr.border_pixel = 0;
	attr.background_pixel = 0;

	win = XCreateWindow(dpy, root,
						0, -1, lp->width, lp->height,
						0, vinfo.depth, InputOutput, vinfo.visual,
						CWColormap|CWBorderPixel|CWBackPixel, &attr);

	isurf = cairo_image_surface_create_from_png("./img/lock.png");
	if (cairo_surface_status(isurf) == CAIRO_STATUS_READ_ERROR) {
		printf("Read error\n");
		exit(1);
	}
	lp->isurf = isurf;
	lp->lwidth = cairo_image_surface_get_width(isurf);
	lp->lheight = cairo_image_surface_get_height(isurf);

	XSelectInput(dpy, win, ButtonPressMask
							|StructureNotifyMask
							|ExposureMask
							|KeyPressMask
							|KeyReleaseMask
							|KeymapStateMask);

	Atom wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", 0);
	XSetWMProtocols(dpy, win, &wm_delete_window, 1);
	XStoreName(dpy, win, "slock_cover");

	classHint = XAllocClassHint();
	if (classHint) {
		classHint->res_name = "slock_cover";
		classHint->res_class = "slock_cover";
	}
	XSetClassHint(dpy, win, classHint);
	XFree(classHint);

	xsurf = cairo_xlib_surface_create(dpy, win, vinfo.visual, lp->width, lp->height);
	cairo = cairo_create(xsurf);

	pl_control(PL_INIT, PL_CAIRO, cairo);
	pl_control(PL_INIT,  PL_LOCK, lp);
	pl_control(PL_INIT,  PL_WINDOW, &win);
	pl_control(PL_INIT,  PL_DISPLAY, dpy);
	pl_control(PL_SET_COLOR|PL_REDRAW, SLATE, NULL);

	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = &TimerHandler;
	sigaction(SIGALRM, &sa, NULL);

	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 10000;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 10000;
	/* setitimer(ITIMER_REAL, &timer, NULL); */

	XMapWindow(dpy, win);
	return win;
}

int LockEventHandler(Display* dpy, XEvent event) {
	switch(event.type) {
		case ClientMessage:
			if (event.xclient.message_type == XInternAtom(dpy, "WM_PROTOCOLS", 1) &&
				(Atom)event.xclient.data.l[0] == XInternAtom(dpy, "WM_DELETE_WINDOW", 1))
			return 1;
		case Expose:
			pl_control(PL_REDRAW, 0, NULL);
			break;
		case ConfigureNotify:
			pl_control(PL_CONFIGURE|PL_REDRAW, 0, &event);
			break;
		case KeymapNotify:
			XRefreshKeyboardMapping(&event.xmapping);
			break;
		case KeyPress:
			pl_control(PL_REDRAW|PL_SET_KEYBOARD,
					event.xkey.keycode % PL_SEG, NULL);
			TimerHandler(-1);
			break;
		default:
			break;
	}
	return 0;
}

#undef PL_SEG
#undef SLATE
#undef PL_COUNTDOWN
#endif
