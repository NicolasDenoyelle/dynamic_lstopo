#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include "lstopo-cairo.h"
#include "monitor.h"
#include "monitor_replay.h"
#include "pwatch.h"

void topo_cairo_perf_boxes(hwloc_topology_t topology, 
monitors_t monitors, hwloc_bitmap_t active, cairo_t *c, struct draw_methods * methods)
{
  unsigned int i, nobj;
  struct monitor_node * box;
  hwloc_obj_t obj;
  for(i=0;i<monitors->count;i++){
    nobj = hwloc_get_nbobjs_by_depth(monitors->topology,monitors->depths[i]);
    while(nobj--){
      box=(struct monitor_node *)(hwloc_get_obj_by_depth(monitors->topology,monitors->depths[i],nobj)->userdata);
      obj = hwloc_get_obj_by_depth(topology,monitors->depths[i],nobj);
      proc_watch_get_watched_in_cpuset(monitors->pw,obj->cpuset,active);
      if(!monitors->pw || !hwloc_bitmap_iszero(active)){
	perf_box_draw(topology, methods, obj, c, obj->depth, (float)box->val, (float)box->max, (float)box->min);
      }
    }
  }
  cairo_show_page(c);
}

void topo_cairo_perf_replay_boxes(hwloc_topology_t topology, 
replay_t replay, cairo_t *c, struct draw_methods * methods)
{
  unsigned int i, nobj;
  hwloc_obj_t obj;
  float val;
  struct replay_node * box;
  for(i=0;i<replay->count;i++){
    nobj = hwloc_get_nbobjs_by_depth(replay->topology,replay->depths[i]);
    while(nobj--){
      obj = hwloc_get_obj_by_depth(replay->topology,replay->depths[i],nobj);
      box=(struct replay_node *)(obj->userdata);
      val = (float)replay_node_get_value(box);
      printf("read_node [%d:%d], value:%f in [%f,%f]\n",replay->depths[i],nobj,val,(float)box->min,(float)box->max);
      perf_box_draw(topology, methods, obj, c, obj->depth, 0.5,1.0,0.0);
    }
  }
  printf("#################################\n");
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
  if(executable){
    int ret;
    pid_t *child = mmap(NULL, sizeof *child, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *child=0;
    pid_t pid2, pid1 = fork();
    if(pid1){
      wait(NULL);
    }
    else if(!pid1){
      pid2=fork();
      if(pid2){
	*child = pid2;
	exit(0);
      }
      else if(!pid2){
	ret = execvp(executable, exe_args);
	if (ret) {
	  fprintf(stderr, "Failed to launch executable \"%s\"\n",
		  executable);
	  perror("execvp");
	  goto exit;
	}
      }
    }
    msync(child, sizeof(*child), MS_SYNC);
    if(*child>0 && !ret){
      Monitors_watch_pid(monitors,*child);
    }
    munmap(child, sizeof *child);
  }

  /* start timer */
  timerfd_settime(itimer_fd,0,&itimer,NULL);
  while(1){
    in_fds = in_fds_original;
    timeout.tv_sec=10;
    timeout.tv_usec=0;
    if(select(nfds, &in_fds, NULL, NULL,&timeout)>0){
      if(FD_ISSET(x11_fd,&in_fds)){
	if(handle_xDisplay(disp,topology,logical,legend,&lastx,&lasty))
	  break;
      }
      if(FD_ISSET(itimer_fd,&in_fds)){
	read(itimer_fd,&buf,sizeof(uint64_t));
	Monitors_update_counters(monitors);
	Monitors_wait_update(monitors);
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

  cairo_t * c = cairo_create(disp->cs);
  struct pollfd pfd;
  pfd.fd = ConnectionNumber(disp->dpy);
  pfd.events = POLLIN;
  
  replay_start(replay);
  while(!replay_is_finished(replay)){
    if(poll(&pfd,1,0)){
      if(handle_xDisplay(disp,topology,logical,legend,&lastx,&lasty))
	goto exit;
    }
    replay_wait_read(replay);
    topo_cairo_perf_replay_boxes(topology, replay, c, &x11_draw_methods);
    XFlush(disp->dpy);
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
  unsigned i;
  for(i=0;i<10;i++){
    Monitors_update_counters(monitors);
    Monitors_wait_update(monitors);
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
  unsigned i;
  for(i=0;i<10;i++){
    Monitors_update_counters(monitors);
    Monitors_wait_update(monitors);
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
  unsigned i;
  for(i=0;i<100;i++){
    Monitors_update_counters(monitors);
    Monitors_wait_update(monitors);
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
  unsigned i;
  for(i=0;i<100;i++){
    Monitors_update_counters(monitors);
    Monitors_wait_update(monitors);
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
