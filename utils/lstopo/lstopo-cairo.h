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
/* Cairo methods */

void topo_cairo_box(void *output, int r, int g, int b, unsigned depth __hwloc_attribute_unused, unsigned x, unsigned width, unsigned y, unsigned height);
void topo_cairo_line(void *output, int r, int g, int b, unsigned depth __hwloc_attribute_unused, unsigned x1, unsigned y1, unsigned x2, unsigned y2);
void topo_cairo_text(void *output, int r, int g, int b, int size, unsigned depth __hwloc_attribute_unused, unsigned x, unsigned y, const char *text);

#if (CAIRO_HAS_PNG_FUNCTIONS + CAIRO_HAS_PDF_SURFACE + CAIRO_HAS_PS_SURFACE + CAIRO_HAS_SVG_SURFACE)
cairo_status_t topo_cairo_write(void *closure, const unsigned char *data, unsigned int length);

#endif /* (CAIRO_HAS_PNG_FUNCTIONS + CAIRO_HAS_PDF_SURFACE + CAIRO_HAS_PS_SURFACE + CAIRO_HAS_SVG_SURFACE) */

void topo_cairo_paint(struct draw_methods *methods, int logical, int legend, hwloc_topology_t topology, cairo_surface_t *cs);

void null_declare_color (void *output __hwloc_attribute_unused, int r __hwloc_attribute_unused, int g __hwloc_attribute_unused, int b __hwloc_attribute_unused);

#endif /* (CAIRO_HAS_XLIB_SURFACE + CAIRO_HAS_PNG_FUNCTIONS + CAIRO_HAS_PDF_SURFACE + CAIRO_HAS_PS_SURFACE + CAIRO_HAS_SVG_SURFACE) */

#if CAIRO_HAS_XLIB_SURFACE
/* X11 back-end */
struct display {
  Display *dpy;
  int scr;
  cairo_surface_t *cs;
  Window top, win;
  Cursor hand;
  unsigned int orig_fontsize, orig_gridsize;
  int screen_width, screen_height;		/** visible part size */
  int last_screen_width, last_screen_height;	/** last visible part size */
  int width, height;				/** total normal display size */
  int x, y;					/** top left corner of the visible part */
};

void x11_create(struct display *disp, int width, int height);
void x11_destroy(struct display *disp);
void * x11_start(void *output __hwloc_attribute_unused, int width, int height);

struct draw_methods x11_draw_methods;

void move_x11(struct display *disp, int logical, int legend, hwloc_topology_t topology);
int handle_xDisplay(struct display *disp, hwloc_topology_t topology, int logical, int legend, int * lastx, int* lasty);
#endif /* CAIRO_HAS_XLIB_SURFACE */

#if CAIRO_HAS_PNG_FUNCTIONS
/* PNG back-end */
void * png_start(void *output __hwloc_attribute_unused, int width, int height);
struct draw_methods png_draw_methods;

#endif /* CAIRO_HAS_PNG_FUNCTIONS */

#if CAIRO_HAS_PDF_SURFACE
/* PDF back-end */
void * pdf_start(void *output, int width, int height);
struct draw_methods pdf_draw_methods;
#endif /* CAIRO_HAS_PDF_SURFACE */

#if CAIRO_HAS_PS_SURFACE
/* PS back-end */
void * ps_start(void *output, int width, int height);
struct draw_methods ps_draw_methods;

#endif /* CAIRO_HAS_PS_SURFACE */

#if CAIRO_HAS_SVG_SURFACE
/* SVG back-end */
void * svg_start(void *output, int width, int height);
struct draw_methods svg_draw_methods;
#endif /* CAIRO_HAS_SVG_SURFACE */

void obj_draw_again(hwloc_topology_t topology, hwloc_obj_t obj, struct draw_methods * methods, int logical, void * output);


