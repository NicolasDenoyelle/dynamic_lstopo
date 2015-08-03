/*
 * Copyright © 2009 CNRS
 * Copyright © 2009-2014 Inria.  All rights reserved.
 * Copyright © 2009-2010, 2014 Université Bordeaux
 * Copyright © 2009-2011 Cisco Systems, Inc.  All rights reserved.
 * See COPYING in top-level directory.
 */

#include <private/autogen/config.h>

#include <cairo.h>
#include <sys/time.h>
#include <sys/timerfd.h>

#if CAIRO_HAS_PDF_SURFACE
#include <cairo-pdf.h>
#endif /* CAIRO_HAS_PDF_SURFACE */

#if CAIRO_HAS_PS_SURFACE
#include <cairo-ps.h>
#endif /* CAIRO_HAS_PS_SURFACE */

#if CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>
#endif /* CAIRO_HAS_SVG_SURFACE */

#ifndef HWLOC_HAVE_X11_KEYSYM
/* In case X11 headers aren't availble, forcefully disable Cairo/Xlib.  */
# undef CAIRO_HAS_XLIB_SURFACE
# define CAIRO_HAS_XLIB_SURFACE 0
#endif

#if CAIRO_HAS_XLIB_SURFACE
#include <cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
/* Avoid Xwindow's definition conflict with Windows' use for fields names.  */
#undef Status
#endif /* CAIRO_HAS_XLIB_SURFACE */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "lstopo.h"

#if (CAIRO_HAS_XLIB_SURFACE + CAIRO_HAS_PNG_FUNCTIONS + CAIRO_HAS_PDF_SURFACE + CAIRO_HAS_PS_SURFACE + CAIRO_HAS_SVG_SURFACE)

struct lstopo_cairo_output {
  struct lstopo_output loutput; /* must be at the beginning */
  cairo_surface_t *surface;
  cairo_t *context;
  unsigned max_x;
  unsigned max_y;
  int drawing;
};

/* Cairo methods */
void topo_cairo_box(void *output, int r, int g, int b, unsigned depth __hwloc_attribute_unused, unsigned x, unsigned width, unsigned y, unsigned height, int highlight);
void topo_cairo_line(void *output, int r, int g, int b, unsigned depth __hwloc_attribute_unused, unsigned x1, unsigned y1, unsigned x2, unsigned y2);
void topo_cairo_text(void *output, int r, int g, int b, int size, unsigned depth __hwloc_attribute_unused, unsigned x, unsigned y, const char *text);
void topo_cairo_textsize(void *_output, const char *text, unsigned textlength __hwloc_attribute_unused, unsigned fontsize __hwloc_attribute_unused, unsigned *width);

#if (CAIRO_HAS_PNG_FUNCTIONS + CAIRO_HAS_PDF_SURFACE + CAIRO_HAS_PS_SURFACE + CAIRO_HAS_SVG_SURFACE)
cairo_status_t topo_cairo_write(void *closure, const unsigned char *data, unsigned int length);
#endif /* (CAIRO_HAS_PNG_FUNCTIONS + CAIRO_HAS_PDF_SURFACE + CAIRO_HAS_PS_SURFACE + CAIRO_HAS_SVG_SURFACE) */

void topo_cairo_paint(struct lstopo_cairo_output *coutput);
#endif /* (CAIRO_HAS_XLIB_SURFACE + CAIRO_HAS_PNG_FUNCTIONS + CAIRO_HAS_PDF_SURFACE + CAIRO_HAS_PS_SURFACE + CAIRO_HAS_SVG_SURFACE) */

#if CAIRO_HAS_XLIB_SURFACE
struct draw_methods x11_draw_methods;
struct lstopo_x11_output {
  struct lstopo_cairo_output coutput; /* must be at the beginning */
  Display *dpy;
  int scr;
  Window top, win;
  Cursor hand;
  unsigned int orig_fontsize, orig_gridsize;
  int screen_width, screen_height;		/** visible part size */
  int last_screen_width, last_screen_height;	/** last visible part size */
  int width, height;				/** total normal display size */
  int x, y;					/** top left corner of the visible part */
};

void x11_create(struct lstopo_x11_output *disp, int width, int height);
void x11_destroy(struct lstopo_x11_output *disp);
void x11_init(void *_disp);
void move_x11(struct lstopo_x11_output *disp);
int handle_xDisplay(struct lstopo_x11_output  *disp, int* lastx, int* lasty, int * state);

#endif /* CAIRO_HAS_XLIB_SURFACE */

#if CAIRO_HAS_PNG_FUNCTIONS
/* PNG back-end */
struct draw_methods png_draw_methods;
void png_init(void *_coutput);
#endif /* CAIRO_HAS_PNG_FUNCTIONS */

#if CAIRO_HAS_PDF_SURFACE
/* PDF back-end */
struct draw_methods pdf_draw_methods;
void pdf_init(void *_coutput);
#endif /* CAIRO_HAS_PDF_SURFACE */

#if CAIRO_HAS_PS_SURFACE
/* PS back-end */
struct draw_methods ps_draw_methods;
void ps_init(void *_coutput);
#endif /* CAIRO_HAS_PS_SURFACE */

#if CAIRO_HAS_SVG_SURFACE
/* SVG back-end */
struct draw_methods svg_draw_methods;
void svg_init(void *_coutput);
#endif /* CAIRO_HAS_SVG_SURFACE */

#if HWLOC_HAVE_MONITOR
void perf_box_draw(struct lstopo_output *loutput, void * output, hwloc_obj_t level, unsigned depth, double value, double variation, double max, double min, int active);
#endif
void obj_draw_again(struct lstopo_output * loutput, void* output, hwloc_obj_t obj, int logical);

