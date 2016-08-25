/*
 * Copyright © 2009 CNRS
 * Copyright © 2009-2016 Inria.  All rights reserved.
 * Copyright © 2009-2012, 2015 Université Bordeaux
 * Copyright © 2009-2011 Cisco Systems, Inc.  All rights reserved.
 * See COPYING in top-level directory.
 */

#include <private/autogen/config.h>
#include <hwloc.h>
#ifdef HWLOC_LINUX_SYS
#include <hwloc/linux.h>
#endif /* HWLOC_LINUX_SYS */
#include <stdio.h>
#ifdef LSTOPO_HAVE_GRAPHICS
#ifdef HWLOC_HAVE_CAIRO
#include <cairo.h>
#endif
#endif
#ifdef HAVE_SETLOCALE
#include <locale.h>
#endif
#include "lstopo.h"

static unsigned int top = 0;

FILE *open_output(const char *filename, int overwrite)
{
  const char *extn;
  struct stat st;

  if (!filename || !strcmp(filename, "-"))
    return stdout;

  extn = strrchr(filename, '.');
  if (filename[0] == '-' && extn == filename + 1)
    return stdout;

  if (!stat(filename, &st) && !overwrite) {
    errno = EEXIST;
    return NULL;
  }

  return fopen(filename, "w");
}


static void
lstopo_add_collapse_attributes(hwloc_topology_t topology)
{
  hwloc_obj_t obj, collapser = NULL;
  unsigned collapsed = 0;
  /* collapse identical PCI devs */
  for(obj = hwloc_get_next_pcidev(topology, NULL); obj; obj = hwloc_get_next_pcidev(topology, obj)) {
    if (collapser) {
      if (!obj->io_arity && !obj->misc_arity
	  && obj->parent == collapser->parent
	  && obj->attr->pcidev.vendor_id == collapser->attr->pcidev.vendor_id
	  && obj->attr->pcidev.device_id == collapser->attr->pcidev.device_id
	  && obj->attr->pcidev.subvendor_id == collapser->attr->pcidev.subvendor_id
	  && obj->attr->pcidev.subdevice_id == collapser->attr->pcidev.subdevice_id) {
	/* collapse another one */
	((struct lstopo_obj_userdata *)obj->userdata)->pci_collapsed = -1;
	collapsed++;
	continue;
      } else if (collapsed > 1) {
	/* end this collapsing */
	((struct lstopo_obj_userdata *)collapser->userdata)->pci_collapsed = collapsed;
	collapser = NULL;
	collapsed = 0;
      }
    }
    if (!obj->io_arity && !obj->misc_arity) {
      /* start a new collapsing */
      collapser = obj;
      collapsed = 1;
    }
  }
  if (collapsed > 1) {
    /* end this collapsing */
    ((struct lstopo_obj_userdata *)collapser->userdata)->pci_collapsed = collapsed;
  }
}

static void
lstopo_populate_userdata(hwloc_obj_t parent)
{
  hwloc_obj_t child;
  struct lstopo_obj_userdata *save = malloc(sizeof(*save));
 
  save->common.buffer = NULL; /* so that it is ignored on XML export */
  save->common.next = parent->userdata;
  memset(save->custom.info, 0, sizeof(save->custom.info));
  save->custom.r = save->custom.g = save->custom.b = -1;
  save->custom.userdata = NULL;
  save->pci_collapsed = 0;
  parent->userdata = save;

  for(child = parent->first_child; child; child = child->next_sibling)
    lstopo_populate_userdata(child);
  for(child = parent->io_first_child; child; child = child->next_sibling)
    lstopo_populate_userdata(child);
  for(child = parent->misc_first_child; child; child = child->next_sibling)
    lstopo_populate_userdata(child);
}

static void
lstopo_destroy_userdata(hwloc_obj_t parent)
{
  hwloc_obj_t child;
  struct lstopo_obj_userdata *save = parent->userdata;

  if (save) {
    parent->userdata = save->common.next;
    free(save);
  }

  for(child = parent->first_child; child; child = child->next_sibling)
    lstopo_destroy_userdata(child);
  for(child = parent->io_first_child; child; child = child->next_sibling)
    lstopo_destroy_userdata(child);
  for(child = parent->misc_first_child; child; child = child->next_sibling)
    lstopo_destroy_userdata(child);
}

#define LSTOPO_VERBOSE_MODE_DEFAULT 1

struct lstopo_output default_output_options(hwloc_topology_t topology, void (*callback)(struct lstopo_custom_box*, hwloc_obj_t)){
    struct lstopo_output loutput;
    unsigned i;
    int err;
    
    loutput.methods = NULL;
    loutput.overwrite = 0;
    loutput.logical = -1;
    loutput.verbose_mode = LSTOPO_VERBOSE_MODE_DEFAULT;
    loutput.ignore_pus = 0;
    loutput.collapse = 1;
    loutput.pid_number = -1;
    loutput.pid = 0;
    loutput.export_synthetic_flags = 0;
    loutput.legend = 1;
    loutput.legend_append = NULL;
    loutput.legend_append_nr = 0;
    loutput.show_distances_only = 0;
    loutput.show_only = HWLOC_OBJ_TYPE_NONE;
    loutput.show_cpuset = 0;
    loutput.show_taskset = 0;
    loutput.fontsize = 10;
    loutput.gridsize = 10;
    for(i=0; i<HWLOC_OBJ_TYPE_MAX; i++)
	loutput.force_orient[i] = LSTOPO_ORIENT_NONE;
    loutput.force_orient[HWLOC_OBJ_PU] = LSTOPO_ORIENT_HORIZ;
    for(i=HWLOC_OBJ_L1CACHE; i<=HWLOC_OBJ_L3ICACHE; i++)
	loutput.force_orient[i] = LSTOPO_ORIENT_HORIZ;
    loutput.force_orient[HWLOC_OBJ_NUMANODE] = LSTOPO_ORIENT_HORIZ;
    loutput.format = LSTOPO_OUTPUT_DEFAULT;
    loutput.output_method = NULL;
    loutput.lstopo_custom_callback = callback;
    if(topology == NULL){
        if(hwloc_topology_init(&topology)){goto return_loutput;}
	if(hwloc_topology_load (topology)){
	    fprintf(stderr, "NULL topology provided to %s and cannot load current one: %s\n", __FUNCTION__, strerror(errno));
	    goto return_loutput;
	}
    }

 return_loutput:
    loutput.topology = topology;
    return loutput;
}

int  lstopo_draw_begin(const char * callname, struct lstopo_output* loutput, enum output_format format){
  hwloc_utils_check_api_version(callname);

  /* enable verbose backends */
  putenv("HWLOC_XML_VERBOSE=1");
  putenv("HWLOC_SYNTHETIC_VERBOSE=1");
#ifdef HAVE_SETLOCALE
  setlocale(LC_ALL, "");
#endif

  if (loutput->pid_number > 0) {
      loutput->pid = hwloc_pid_from_number(loutput->pid_number, 0);
      if (hwloc_topology_set_pid(loutput->topology, loutput->pid)) {
	  perror("Setting target pid");
	  return -1;
      }
  }

  /* if  the output format wasn't enforced, think a bit about what the user probably want */
  if(format == LSTOPO_OUTPUT_DEFAULT) {
      if (loutput->show_cpuset
	  || loutput->show_only != HWLOC_OBJ_TYPE_NONE
	  || loutput->show_distances_only
	  || loutput->verbose_mode != LSTOPO_VERBOSE_MODE_DEFAULT)
	  format = LSTOPO_OUTPUT_CONSOLE;
  }

  if (format == LSTOPO_OUTPUT_ERROR)
      return -1;

  if (loutput->logical == -1) {
      if (format == LSTOPO_OUTPUT_CONSOLE)
	  loutput->logical = 1;
    else if (format != LSTOPO_OUTPUT_DEFAULT)
      loutput->logical = 0;
  }

  loutput->format = format;

  lstopo_populate_userdata(hwloc_get_root_obj(loutput->topology));

  if (format != LSTOPO_OUTPUT_XML && loutput->collapse)
      lstopo_add_collapse_attributes(loutput->topology);
  
  switch (format) {
  case LSTOPO_OUTPUT_DEFAULT:
#ifdef LSTOPO_HAVE_GRAPHICS
#if CAIRO_HAS_XLIB_SURFACE && defined HWLOC_HAVE_X11_KEYSYM
      if (getenv("DISPLAY")) {
	  if (loutput->logical == -1)
	      loutput->logical = 0;
	  loutput->output_method = output_x11;
      } else
#endif /* CAIRO_HAS_XLIB_SURFACE */
#ifdef HWLOC_WIN_SYS
	  {
	      if (loutput->logical == -1)
		  loutput->logical = 0;
	      loutput->output_method = output_windows;
      }
#endif
#endif /* !LSTOPO_HAVE_GRAPHICS */
#if !defined HWLOC_WIN_SYS || !defined LSTOPO_HAVE_GRAPHICS
      {
        if (loutput->logical == -1)
          loutput->logical = 1;
        loutput->output_method = output_console;
      }
#endif
      break;

    case LSTOPO_OUTPUT_CONSOLE:
      loutput->output_method = output_console;
      break;
    case LSTOPO_OUTPUT_SYNTHETIC:
      loutput->output_method = output_synthetic;
      break;
    case LSTOPO_OUTPUT_ASCII:
      loutput->output_method = output_ascii;
      break;
    case LSTOPO_OUTPUT_FIG:
      loutput->output_method = output_fig;
      break;
#ifdef LSTOPO_HAVE_GRAPHICS
# if CAIRO_HAS_PNG_FUNCTIONS
    case LSTOPO_OUTPUT_PNG:
      loutput->output_method = output_png;
      break;
# endif /* CAIRO_HAS_PNG_FUNCTIONS */
# if CAIRO_HAS_PDF_SURFACE
    case LSTOPO_OUTPUT_PDF:
      loutput->output_method = output_pdf;
      break;
# endif /* CAIRO_HAS_PDF_SURFACE */
# if CAIRO_HAS_PS_SURFACE
    case LSTOPO_OUTPUT_PS:
      loutput->output_method = output_ps;
      break;
#endif /* CAIRO_HAS_PS_SURFACE */
#if CAIRO_HAS_SVG_SURFACE
    case LSTOPO_OUTPUT_SVG:
      loutput->output_method = output_svg;
      break;
#endif /* CAIRO_HAS_SVG_SURFACE */
#endif /* LSTOPO_HAVE_GRAPHICS */
    case LSTOPO_OUTPUT_XML:
      loutput->output_method = output_xml;
      break;
    default:
      fprintf(stderr, "file format not supported\n");
      return -1;
  }
  return 0;
}

void lstopo_draw_end(struct lstopo_output* loutput){
    unsigned i;
    lstopo_destroy_userdata(hwloc_get_root_obj(loutput->topology));
    for(i=0; i<loutput->legend_append_nr; i++)
	free(loutput->legend_append[i]);
    free(loutput->legend_append);
    
    if (loutput->methods && loutput->methods->end)
	loutput->methods->end(loutput);
    
    lstopo_destroy_userdata(hwloc_get_root_obj(loutput->topology));
    hwloc_utils_userdata_free_recursive(hwloc_get_root_obj(loutput->topology));
    hwloc_topology_destroy(loutput->topology);
    
    for(i=0; i<loutput->legend_append_nr; i++)
	free(loutput->legend_append[i]);
    free(loutput->legend_append);
}


void lstopo_draw_topology(struct lstopo_output* loutput, const char * filename, int block){
    loutput->output_method(loutput, filename);
    if (loutput->methods && loutput->methods->iloop)
	loutput->methods->iloop(loutput, block);
}


void lstopo_draw_update(struct lstopo_output* loutput){
    if (loutput->methods && loutput->methods->iloop)
	loutput->methods->iloop(loutput, 0);
}

