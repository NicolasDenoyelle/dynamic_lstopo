#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include "lstopo-cairo.h"
#include "monitor.h"
#include "monitor_replay.h"
#include "pwatch.h"

void topo_cairo_perf_boxes(hwloc_topology_t topology, 
			     monitors_t monitors, hwloc_bitmap_t active, cairo_t *c, struct draw_methods * methods)
{
  unsigned int i, nobj;
  hwloc_obj_t obj;
  double val, variation;
  struct monitor_node * box;
  struct dyna_save * ds;
  for(i=0;i<monitors->count;i++){
    nobj = hwloc_get_nbobjs_by_depth(monitors->topology,monitors->depths[i]);
    while(nobj--){
      obj = hwloc_get_obj_by_depth(topology,monitors->depths[i],nobj);
      box = ((struct monitor_node*)(hwloc_get_obj_by_depth(monitors->topology,monitors->depths[i],nobj)->userdata));
      val = box->val;
      variation = val - box->val1;
      if(!monitors->pw)
	perf_box_draw(topology, methods, obj, c, obj->depth, val, variation, monitors->max[i], monitors->min[i]);
      else{
	proc_watch_get_watched_in_cpuset(monitors->pw,obj->cpuset,active);
	if(!hwloc_bitmap_iszero(active)){
	  box->userdata=(void*)0;
	  perf_box_draw(topology, methods, obj, c, obj->depth, val, variation, monitors->max[i], monitors->min[i]);
	  if(obj->type == HWLOC_OBJ_CORE || obj->type == HWLOC_OBJ_PACKAGE)
	    obj_draw_again(topology, obj->first_child, methods, 0, c);
	}
	else if(!box->userdata){
	  box->userdata = (void*)1;
	  obj_draw_again(topology, obj, methods, 0, c);
	}
      }
    }
  }
  cairo_show_page(c);
}

void topo_cairo_perf_replay_boxes(hwloc_topology_t topology, 
replay_t replay, cairo_t *c, struct draw_methods * methods)
{
  unsigned int i, old_val;
  hwloc_obj_t obj;
  struct value_line vl;
  replay_get_value(replay,&vl);
  obj = replay->nodes[vl.id];
  old_val = ((double*)obj->userdata)[1];
  obj = hwloc_get_obj_by_depth(topology,obj->depth,obj->logical_index);
  perf_box_draw(topology, methods, obj, c, obj->depth, 
		vl.value, vl.value-old_val, 
		replay->max[obj->depth], 
		replay->min[obj->depth]);
  cairo_show_page(c);
}

#if CAIRO_HAS_XLIB_SURFACE
void output_x11_perf(hwloc_topology_t topology, const char *filename __hwloc_attribute_unused, int overwrite __hwloc_attribute_unused, int logical, int legend, int verbose_mode __hwloc_attribute_unused, monitors_t monitors, unsigned long refresh_usec, char * executable, char * exe_args[])
{
  struct timeval timeout;
  char buf[sizeof(uint64_t)];

  /* draw initial topology */
  struct display *disp;
  disp = output_draw_start(&x11_draw_methods, logical, legend, topology, NULL);
  int lastx = disp->x, lasty = disp->y;
  topo_cairo_paint(&x11_draw_methods, logical, legend, topology, disp->cs);
  /* flush windows*/
  XMoveWindow(disp->dpy, disp->win, -disp->x, -disp->y);
  XFlush(disp->dpy);
  cairo_t * c = cairo_create(disp->cs);

  /* passed to draw perf boxes methods to avoid drawing unactive boxes */
  hwloc_bitmap_t active;
  active= hwloc_bitmap_dup(hwloc_topology_get_topology_cpuset(topology));

  /* set timer interval for update */
  struct itimerspec itimer;
  itimer.it_interval.tv_sec=refresh_usec/1000000; 
  itimer.it_interval.tv_nsec=(refresh_usec%1000000)*1000; 
  itimer.it_value.tv_sec=itimer.it_interval.tv_sec;
  itimer.it_value.tv_nsec=itimer.it_interval.tv_nsec; 
 
  /* select between x11event and alarm */
  fd_set in_fds, in_fds_original;
  FD_ZERO(&in_fds_original);  
  int x11_fd = ConnectionNumber(disp->dpy);
  int itimer_fd = timerfd_create(CLOCK_REALTIME,0);
  if(itimer_fd==-1){
    perror("itimer");
    goto exit;
  }
  FD_SET(x11_fd, &in_fds_original);
  FD_SET(itimer_fd, &in_fds_original);
  int nfds = x11_fd > itimer_fd ? x11_fd+1 : itimer_fd+1;  
  
  /* start monitoring activity */
  Monitors_start(monitors);

  /* start executable to watch */
  int pid=0;
  if(executable){
    pid = start_executable(executable,exe_args);
    Monitors_watch_pid(monitors,pid);
    printf("monitoring pid %d\n",pid);
  }

  /* start timer */
  timerfd_settime(itimer_fd,0,&itimer,NULL);
  while(pid ? kill(pid,0)==0 : 1){
    in_fds = in_fds_original;
    timeout.tv_sec=10;
    timeout.tv_usec=0;
    if(select(nfds, &in_fds, NULL, NULL,&timeout)>0){
      if(FD_ISSET(x11_fd,&in_fds)){
	if(handle_xDisplay(disp,topology,logical,legend,&lastx,&lasty))
	  break;
      }
      if(FD_ISSET(itimer_fd,&in_fds)){
	if(read(itimer_fd,&buf,sizeof(uint64_t))==-1){
	  perror("read");
	}
	proc_watch_update(monitors->pw);
	Monitors_update_counters(monitors);
	topo_cairo_perf_boxes(topology, monitors, active, c, &x11_draw_methods);
	XFlush(disp->dpy);
      }
    }
  }
    
 exit:;
  hwloc_bitmap_free(active); 
  cairo_destroy(c);
  x11_destroy(disp);
  XDestroyWindow(disp->dpy, disp->top);
  XFreeCursor(disp->dpy, disp->hand);
  XCloseDisplay(disp->dpy);
  free(disp);
}

void output_x11_perf_replay(hwloc_topology_t topology, const char *filename __hwloc_attribute_unused, int overwrite __hwloc_attribute_unused, int logical, int legend, int verbose_mode __hwloc_attribute_unused, replay_t replay)
{
  /* draw initial topology */
  struct display *disp;
  disp = output_draw_start(&x11_draw_methods, logical, legend, topology, NULL);
  int lastx = disp->x, lasty = disp->y;
  topo_cairo_paint(&x11_draw_methods, logical, legend, topology, disp->cs);
  /* flush windows*/
  XMoveWindow(disp->dpy, disp->win, -disp->x, -disp->y);
  XFlush(disp->dpy);

  cairo_t * c = cairo_create(disp->cs);

  /* select between x11event and alarm */
  char buf[sizeof(uint64_t)];
  struct timeval timeout;
  fd_set in_fds, in_fds_original;
  FD_ZERO(&in_fds_original);  
  int x11_fd = ConnectionNumber(disp->dpy);
  FD_SET(x11_fd, &in_fds_original);
  FD_SET(replay->update_read_fd, &in_fds_original);
  int nfds = x11_fd > replay->update_read_fd ? x11_fd+1 : replay->update_read_fd+1;  
  
  replay_start(replay);
  while(!replay_is_finished(replay)){
    in_fds = in_fds_original;
    timeout.tv_sec=1 ;
    timeout.tv_usec=0;
    if(select(nfds, &in_fds, NULL, NULL,&timeout)>0){
      if(FD_ISSET(x11_fd,&in_fds)){
	if(handle_xDisplay(disp,topology,logical,legend,&lastx,&lasty))
	  break;
      }
      if(FD_ISSET(replay->update_read_fd,&in_fds)){
	if(read(replay->update_read_fd,&buf,sizeof(uint64_t))==-1){
	  perror("read");
	}
	topo_cairo_perf_replay_boxes(topology, replay, c, &x11_draw_methods);
    	XFlush(disp->dpy);
      }
    }
  }
    
 exit:;
  cairo_destroy(c);
  x11_destroy(disp);
  XDestroyWindow(disp->dpy, disp->top);
  XFreeCursor(disp->dpy, disp->hand);
  XCloseDisplay(disp->dpy);
  free(disp);
}

#endif /* CAIRO_HAS_XLIB_SURFACE */

#if CAIRO_HAS_PNG_FUNCTIONS
/* PNG back-end */
void
output_png_perf(hwloc_topology_t topology, const char *filename, int overwrite, int logical, int legend, int verbose_mode __hwloc_attribute_unused, monitors_t monitors, unsigned long refresh_usec, char * executable, char * exe_args[])
{
  FILE *output = open_output(filename, overwrite);
  cairo_surface_t *cs;

  if (!output) {
    fprintf(stderr, "Failed to open %s for writing (%s)\n", filename, strerror(errno));
    return;
  }

  cs = output_draw_start(&png_draw_methods, logical, legend, topology, output);
  topo_cairo_paint(&png_draw_methods, logical, legend, topology, cs);

  hwloc_bitmap_t active = hwloc_bitmap_dup(hwloc_topology_get_topology_cpuset(topology));
  cairo_t *c;
  c = cairo_create(cs);
  Monitors_start(monitors);
  if(executable){
    int pid=0;
    pid = start_executable(executable,exe_args);
    Monitors_watch_pid(monitors,pid);
    proc_watch_update(monitors->pw);
    printf("monitoring pid %d\n",pid);
    waitpid(pid,NULL,0);
    Monitors_update_counters(monitors);
  }
  else{
    unsigned i;
    for(i=0;i<10;i++){
      Monitors_update_counters(monitors);
    }
  }
  topo_cairo_perf_boxes(topology, monitors, active, c, &png_draw_methods);
  cairo_destroy(c);

  cairo_surface_write_to_png_stream(cs, topo_cairo_write, output);
  cairo_surface_destroy(cs);
  hwloc_bitmap_free(active);
  if (output != stdout)
    fclose(output);
}
#endif /* CAIRO_HAS_PNG_FUNCTIONS */

#if CAIRO_HAS_PDF_SURFACE
/* PDF back-end */
void
output_pdf_perf(hwloc_topology_t topology, const char *filename __hwloc_attribute_unused, int overwrite __hwloc_attribute_unused, int logical, int legend, int verbose_mode __hwloc_attribute_unused, monitors_t monitors, unsigned long refresh_usec, char * executable, char * exe_args[])
{
  hwloc_bitmap_t active = hwloc_bitmap_dup(hwloc_topology_get_topology_cpuset(topology));
  FILE *output = open_output(filename, overwrite);
  cairo_surface_t *cs;

  if (!output) {
    fprintf(stderr, "Failed to open %s for writing (%s)\n", filename, strerror(errno));
    return;
  }

  cs = output_draw_start(&pdf_draw_methods, logical, legend, topology, output);
  cairo_t *c;
  c = cairo_create(cs);
  Monitors_start(monitors);
  if(executable){
    int pid=0;
    pid = start_executable(executable,exe_args);
    Monitors_watch_pid(monitors,pid);
    printf("monitoring pid %d\n",pid);
    Monitors_update_counters(monitors);
    proc_watch_update(monitors->pw);
    waitpid(pid,NULL,0);
    Monitors_update_counters(monitors);
  }
  else{
    unsigned i;
    for(i=0;i<10;i++){
      Monitors_update_counters(monitors);
    }
  }
  output_draw( &pdf_draw_methods, logical, legend, topology, c);
  topo_cairo_perf_boxes(topology, monitors, active, c, &pdf_draw_methods);
  hwloc_bitmap_free(active);

  cairo_destroy(c);
  cairo_surface_flush(cs);
  cairo_surface_destroy(cs);
  if (output != stdout)
    fclose(output);
}
#endif /* CAIRO_HAS_PDF_SURFACE */

#if CAIRO_HAS_PS_SURFACE
/* PS back-end */
void
output_ps_perf(hwloc_topology_t topology, const char *filename, int overwrite, int logical, int legend, int verbose_mode __hwloc_attribute_unused, monitors_t monitors, unsigned long refresh_usec, char * executable, char * exe_args[])
{
  FILE *output = open_output(filename, overwrite);
  cairo_surface_t *cs;

  if (!output) {
    fprintf(stderr, "Failed to open %s for writing (%s)\n", filename, strerror(errno));
    return;
  }

  cs = output_draw_start(&ps_draw_methods, logical, legend, topology, output);
  hwloc_bitmap_t active = hwloc_bitmap_dup(hwloc_topology_get_topology_cpuset(topology));
  cairo_t *c;
  c = cairo_create(cs);
  Monitors_start(monitors);
  if(executable){
    int pid=0;
    pid = start_executable(executable,exe_args);
    Monitors_watch_pid(monitors,pid);
    printf("monitoring pid %d\n",pid);
    waitpid(pid,NULL,0);
    Monitors_update_counters(monitors);
    proc_watch_update(monitors->pw);
  }
  else{
    unsigned i;
    for(i=0;i<10;i++){
      Monitors_update_counters(monitors);
    }
  }
  output_draw( &ps_draw_methods, logical, legend, topology, c);
  topo_cairo_perf_boxes(topology, monitors, active, c, &png_draw_methods);
  hwloc_bitmap_free(active);
  cairo_destroy(c);
  cairo_surface_flush(cs);
  cairo_surface_destroy(cs);
  if (output != stdout)
    fclose(output);
}
#endif /* CAIRO_HAS_PS_SURFACE */


#if CAIRO_HAS_SVG_SURFACE
/* SVG back-end */

void
output_svg_perf(hwloc_topology_t topology, const char *filename, int overwrite, int logical, int legend, int verbose_mode __hwloc_attribute_unused, monitors_t monitors, unsigned long refresh_usec, char * executable, char * exe_args[])
{
  FILE *output;
  cairo_surface_t *cs;

  output = open_output(filename, overwrite);
  if (!output) {
    fprintf(stderr, "Failed to open %s for writing (%s)\n", filename, strerror(errno));
    return;
  }

  cs = output_draw_start(&svg_draw_methods, logical, legend, topology, output);
  hwloc_bitmap_t active = hwloc_bitmap_dup(hwloc_topology_get_topology_cpuset(topology));
  cairo_t *c;
  c = cairo_create(cs);
  Monitors_start(monitors);
  if(executable){
    int pid=0;
    pid = start_executable(executable,exe_args);
    Monitors_watch_pid(monitors,pid);
    printf("monitoring pid %d\n",pid);
    waitpid(pid,NULL,0);
    Monitors_update_counters(monitors);
    proc_watch_update(monitors->pw);
  }
  else{
    unsigned i;
    for(i=0;i<10;i++){
      Monitors_update_counters(monitors);
    }
  }
  output_draw( &svg_draw_methods, logical, legend, topology, c);
  topo_cairo_perf_boxes(topology, monitors, active, c, &png_draw_methods);
  cairo_destroy(c);

  cairo_surface_flush(cs);
  cairo_surface_destroy(cs);

  if (output != stdout)
    fclose(output);
}
#endif /* CAIRO_HAS_SVG_SURFACE */
