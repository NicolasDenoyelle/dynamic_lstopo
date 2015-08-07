#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include "lstopo-cairo.h"
#include "monitor.h"
#include "monitor_replay.h"
#include "pwatch.h"



static void
topo_cairo_perf_boxes(struct lstopo_cairo_output * coutput, monitors_t monitors, hwloc_bitmap_t active)
{
  unsigned int i, nobj;
  hwloc_obj_t obj=NULL,sibling=NULL;
  double val, variation;
  hwloc_topology_t topology = coutput->loutput.topology;
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
	perf_box_draw(&coutput->loutput, coutput, obj, obj->depth, val, variation, monitors->max[i], monitors->min[i], 1, monitors->logscale[i]);
      else{
	proc_watch_get_watched_in_cpuset(monitors->pw,obj->cpuset,active);
	if(hwloc_bitmap_iszero(active)){
	  if(box->userdata == (void*)0) //not already drawn unactive
	    perf_box_draw(&coutput->loutput, coutput, obj, obj->depth, val, variation, monitors->max[i], monitors->min[i],0, monitors->logscale[i]);
	  box->userdata == (void*)1;
	}
	else{
	  box->userdata = (void*)0;
	  perf_box_draw(&coutput->loutput, coutput, obj, obj->depth, val, variation, monitors->max[i], monitors->min[i],!hwloc_bitmap_iszero(active), monitors->logscale[i]);
	}
      }
    }
  }

  /* redraw deeper objects overlapped by boxes */
  while((obj=obj->first_child)!=NULL){
    nobj = hwloc_get_nbobjs_by_depth(topology,obj->depth);
    while(nobj--){
      obj = hwloc_get_obj_by_depth(topology,obj->depth,nobj);
      obj_draw_again(&coutput->loutput, coutput, obj, 0);
    }
  }
}

static void 
topo_cairo_perf_replay_boxes(struct lstopo_cairo_output * coutput, replay_t replay)
{
  unsigned int i, old_val;
  hwloc_obj_t obj;
  struct value_line vl;
  replay_get_value(replay,&vl);
  obj = replay->nodes[vl.id];
  old_val = ((double*)obj->userdata)[1];
  obj = hwloc_get_obj_by_depth(coutput->loutput.topology,obj->depth,obj->logical_index);
  perf_box_draw(&coutput->loutput, coutput, obj, obj->depth, 
		vl.value, vl.value-old_val, 
		replay->max[obj->depth], 
		replay->min[obj->depth],
		1, replay->logscale[obj->depth]);
}


static void
static_app_monitor(struct lstopo_output * loutput, const char *filename, monitors_t m, struct perf_attributes * perf_attributes)
{
  FILE * output = open_output(filename, loutput->overwrite);
  if (!output) {
    fprintf(stderr, "Failed to open %s for writing (%s)\n", filename, strerror(errno));
    return;
  }

  if(perf_attributes->executable){
    m->accum=1;
    int pid=0;
    pid = start_executable(perf_attributes->executable,perf_attributes->exe_args);
    if(!perf_attributes->perf_whole_machine)
      Monitors_watch_pid(m,pid);
    Monitors_start(m);
    while(waitpid(pid, NULL, WNOHANG)==0){
      proc_watch_update(m->pw);
      Monitors_update_counters(m);
      usleep(perf_attributes->refresh_usec);
    }
    waitpid(pid,NULL,0);
    Monitors_update_counters(m);
  }
  else{
    Monitors_start(m);
    unsigned i;
    for(i=0;i<10;i++){
      Monitors_update_counters(m);
    }
  }

  hwloc_bitmap_t active = hwloc_bitmap_alloc();


  struct lstopo_cairo_output coutput;
  memset(&coutput, 0, sizeof(coutput));
  memcpy(&coutput.loutput, loutput, sizeof(*loutput));
  coutput.loutput.file = output;
  output_draw_start(&coutput.loutput);
  coutput.context = cairo_create(coutput.surface);
  cairo_set_font_size(coutput.context, fontsize);
  output_draw(&coutput.loutput);
  topo_cairo_perf_boxes(&coutput, m, active);
  cairo_show_page(coutput.context);
  cairo_destroy(coutput.context);

  if(loutput->methods == &png_draw_methods)
    cairo_surface_write_to_png_stream(coutput.surface, topo_cairo_write, output);
  cairo_surface_flush(coutput.surface);
  cairo_surface_destroy(coutput.surface);
  if (output != stdout)
    fclose(output);
  hwloc_bitmap_free(active);
  return;
}
 
static void
static_replay(struct lstopo_output *loutput, const char *filename, replay_t replay)
{
  /*initialize cairo*/
  FILE * output = open_output(filename, loutput->overwrite);
  if (!output) {
    fprintf(stderr, "Failed to open %s for writing (%s)\n", filename, strerror(errno));
    return;
  }

  struct lstopo_cairo_output coutput;
  memset(&coutput, 0, sizeof(coutput));
  memcpy(&coutput.loutput, loutput, sizeof(*loutput));
  coutput.loutput.file = output;
  
  output_draw_start(&coutput.loutput);

  cairo_t * c = cairo_create(coutput.surface);
  coutput.context = c;

  cairo_set_font_size(coutput.context, fontsize);
  output_draw(&coutput.loutput);

  /* read replay values */
  struct value_line vl;
  char buf[sizeof(uint64_t)];
  struct timeval timeout;
  fd_set in_fds, in_fds_original;
  FD_ZERO(&in_fds_original);  
  FD_SET(replay->update_read_fd, &in_fds_original);
  int nfds = replay->update_read_fd+1;
  replay->accumulate=1;
  replay_start(replay);
  while(!replay_is_finished(replay)){
    in_fds = in_fds_original;
    timeout.tv_sec=1 ;
    timeout.tv_usec=0;
    if(select(nfds, &in_fds, NULL, NULL,&timeout)>0){
      if(FD_ISSET(replay->update_read_fd,&in_fds)){
	if(read(replay->update_read_fd,&buf,sizeof(uint64_t))==-1){
	  perror("read");
	}
	replay_get_value(replay,&vl);
      }
    }
  }

  /* draw replay values */
  unsigned i;
  hwloc_obj_t obj;
  double old_val, val;
  unsigned topo_depth = hwloc_topology_get_depth(replay->topology);
  unsigned n_obj = topo_depth * hwloc_get_nbobjs_by_depth(replay->topology,topo_depth-1);
  unsigned long max_depth=0;
  coutput.context = c;

  for(i=0;i<n_obj;i++){
    obj = replay->nodes[i];
    if(obj && obj->userdata){
      old_val = ((double*)obj->userdata)[1];
      val = ((double*)obj->userdata)[0];
      obj=hwloc_get_obj_by_depth(coutput.loutput.topology,obj->depth,obj->logical_index);
      perf_box_draw(&coutput.loutput, &coutput, obj, obj->depth, 
		    val, old_val, 
		    replay->max[obj->depth], 
		    replay->min[obj->depth],
		    1, replay->logscale[obj->depth]);
      max_depth = obj->depth>max_depth? obj->depth : max_depth;
    }
  }
  for(i=max_depth+1;i<topo_depth;i++){
    n_obj = hwloc_get_nbobjs_by_depth(loutput->topology,topo_depth);
    while(n_obj--){
      obj = hwloc_get_obj_by_depth(loutput->topology,i,n_obj);
      obj_draw_again(loutput, c, obj, loutput->logical);
    }
  }

  /*clean up*/
  cairo_show_page(c);
  cairo_destroy(c);
  cairo_surface_flush(coutput.surface);
  if(loutput->methods == &png_draw_methods)
    cairo_surface_write_to_png_stream(coutput.surface, topo_cairo_write, output);
  cairo_surface_destroy(coutput.surface);

  if (output != stdout)
    fclose(output);
}

#if CAIRO_HAS_XLIB_SURFACE
void 
output_x11_perf(struct lstopo_output * loutput, struct perf_attributes * attr, const char *filename, monitors_t monitors)
{
  struct lstopo_x11_output _disp, *disp = &_disp;
  struct lstopo_cairo_output *coutput;
  int lastx, lasty;
  int state = 0;
  
  coutput = &disp->coutput;
  memset(coutput, 0, sizeof(*coutput));
  memcpy(&coutput->loutput, loutput, sizeof(*loutput));
  coutput->loutput.methods = &x11_draw_methods;
 
  output_draw_start(&coutput->loutput);
  lastx = disp->x;
  lasty = disp->y;
  topo_cairo_paint(coutput);

  cairo_t * c = cairo_create(coutput->surface);

  struct timeval timeout;
  char buf[sizeof(uint64_t)];
  /* passed to draw perf boxes methods to avoid drawing unactive boxes */
  hwloc_bitmap_t active;
  active= hwloc_bitmap_dup(hwloc_topology_get_topology_cpuset(loutput->topology));
  /* set timer interval for update */
  struct itimerspec itimer;
  itimer.it_interval.tv_sec=attr->refresh_usec/1000000;
  itimer.it_interval.tv_nsec=(attr->refresh_usec%1000000)*1000;
  itimer.it_value.tv_sec=itimer.it_interval.tv_sec;
  itimer.it_value.tv_nsec=itimer.it_interval.tv_nsec;
 
  /* select between x11event and alarm */
  fd_set in_fds, in_fds_original;
  FD_ZERO(&in_fds_original);
  int x11_fd = ConnectionNumber(disp->dpy);
  int itimer_fd = timerfd_create(CLOCK_REALTIME,0);
  if(itimer_fd==-1){
    perror("itimer");
    exit(1);
  }
  FD_SET(x11_fd, &in_fds_original);
  FD_SET(itimer_fd, &in_fds_original);
  int nfds = x11_fd > itimer_fd ? x11_fd+1 : itimer_fd+1;

  /* start timer */
  timerfd_settime(itimer_fd,0,&itimer,NULL);

  /* start executable to watch */
  int pid=0;
  if(attr->executable){
    pid = start_executable(attr->executable,attr->exe_args);
    if(!attr->perf_whole_machine){
      Monitors_watch_pid(monitors,pid);
      printf("monitoring pid %d of %s\n",pid,attr->executable);
    }
  }

  /* start monitoring activity */
  Monitors_start(monitors);
  int user_stopped=0;

  while(pid ? waitpid(pid, NULL, WNOHANG)==0 : 1){
    in_fds = in_fds_original;
    timeout.tv_sec=1;
    timeout.tv_usec=0;
    if(select(nfds, &in_fds, NULL, NULL,&timeout)>0){
      if(FD_ISSET(x11_fd,&in_fds)){
  	if(handle_xDisplay(disp,&lastx,&lasty,&state)){
  	  user_stopped=1;
  	  break;
  	}
      }
      if(FD_ISSET(itimer_fd,&in_fds)){
  	if(read(itimer_fd,&buf,sizeof(uint64_t))==-1){
  	  perror("read");
  	}
  	proc_watch_update(monitors->pw);
  	Monitors_update_counters(monitors);
	coutput->context=c;
	topo_cairo_perf_boxes(coutput, monitors, active);
	cairo_show_page(c);
	cairo_surface_flush(coutput->surface);
	XFlush(disp->dpy);
      }
    }
  }
  if(pid)
    waitpid(pid,NULL,0);

  if(!user_stopped){
    while(!handle_xDisplay(disp,&lastx,&lasty,&state));
  }

  hwloc_bitmap_free(active);
  cairo_destroy(c);
  x11_destroy(disp);
  XDestroyWindow(disp->dpy, disp->top);
  XFreeCursor(disp->dpy, disp->hand);
  XCloseDisplay(disp->dpy);
}

void 
output_x11_perf_replay(struct lstopo_output * loutput, const char *filename __hwloc_attribute_unused, replay_t replay)
{
  struct lstopo_x11_output _disp, *disp = &_disp;
  struct lstopo_cairo_output *coutput;
  int lastx, lasty;
  int state = 0;
  
  loutput->methods = &x11_draw_methods;
  coutput = &disp->coutput;
  memset(coutput, 0, sizeof(*coutput));
  memcpy(&coutput->loutput, loutput, sizeof(*loutput));

  output_draw_start(&coutput->loutput);
  lastx = disp->x;
  lasty = disp->y;
  topo_cairo_paint(coutput);
  cairo_t * c = cairo_create(coutput->surface);


  /* select between x11event and alarm */
  char buf[sizeof(uint64_t)];
  struct timeval timeout;
  fd_set in_fds, in_fds_original;
  FD_ZERO(&in_fds_original);  
  int x11_fd = ConnectionNumber(disp->dpy);
  FD_SET(x11_fd, &in_fds_original);
  FD_SET(replay->update_read_fd, &in_fds_original);
  int nfds = x11_fd > replay->update_read_fd ? x11_fd+1 : replay->update_read_fd+1;  
  int user_stopped = 0;

  replay_start(replay);
  while(!replay_is_finished(replay)){
    in_fds = in_fds_original;
    timeout.tv_sec=1 ;
    timeout.tv_usec=0;
    if(select(nfds, &in_fds, NULL, NULL,&timeout)>0){
      if(FD_ISSET(x11_fd,&in_fds)){
	if(handle_xDisplay(disp,&lastx,&lasty,&state)){
	  user_stopped=1;
	  break;
	}
      }
      if(FD_ISSET(replay->update_read_fd,&in_fds)){
	if(read(replay->update_read_fd,&buf,sizeof(uint64_t))==-1){
	  perror("read");
	}
	coutput->context=c;
	topo_cairo_perf_replay_boxes(coutput, replay);
	cairo_show_page(c);
	cairo_surface_flush(coutput->surface);
	XFlush(disp->dpy);
      }
    }
  }
  if(!user_stopped){
    while(!handle_xDisplay(disp,&lastx,&lasty,&state));
  }

  cairo_destroy(c);
  x11_destroy(disp);
  XDestroyWindow(disp->dpy, disp->top);
  XFreeCursor(disp->dpy, disp->hand);
  XCloseDisplay(disp->dpy);
}

#endif /* CAIRO_HAS_XLIB_SURFACE */


#if CAIRO_HAS_PNG_FUNCTIONS
/* PNG back-end */
inline void
output_png_perf(struct lstopo_output * loutput, struct perf_attributes *attr, const char *filename, monitors_t monitors)
{
  loutput->methods = &png_draw_methods;
  static_app_monitor(loutput, filename, monitors, attr);
}

inline void 
output_png_perf_replay(struct lstopo_output *loutput, const char *filename, replay_t replay)
{
  loutput->methods = &png_draw_methods;
  static_replay(loutput, filename, replay);
}
#endif /* CAIRO_HAS_PNG_FUNCTIONS */

#if CAIRO_HAS_PDF_SURFACE
/* PDF back-end */
inline void
output_pdf_perf(struct lstopo_output * loutput, struct perf_attributes *attr, const char *filename, monitors_t monitors)
{
  loutput->methods = &pdf_draw_methods;
  static_app_monitor(loutput, filename, monitors, attr);
}

inline void 
output_pdf_perf_replay(struct lstopo_output *loutput, const char *filename, replay_t replay)
{
  loutput->methods = &pdf_draw_methods;
  static_replay(loutput, filename, replay);
}

#endif /* CAIRO_HAS_PDF_SURFACE */

#if CAIRO_HAS_PS_SURFACE
/* PS back-end */
inline void
output_ps_perf(struct lstopo_output * loutput, struct perf_attributes *attr, const char *filename, monitors_t monitors)
{
  loutput->methods = &ps_draw_methods;
  static_app_monitor(loutput, filename, monitors, attr);
}

inline void 
output_ps_perf_replay(struct lstopo_output *loutput, const char *filename, replay_t replay)
{
  loutput->methods = &ps_draw_methods;
  static_replay(loutput, filename, replay);
}
#endif /* CAIRO_HAS_PS_SURFACE */


#if CAIRO_HAS_SVG_SURFACE
/* SVG back-end */
inline void
output_svg_perf(struct lstopo_output * loutput, struct perf_attributes *attr, const char *filename, monitors_t monitors)
{
  loutput->methods = &svg_draw_methods;
  static_app_monitor(loutput, filename, monitors, attr);
}

inline void 
output_svg_perf_replay(struct lstopo_output *loutput, const char *filename, replay_t replay)
{
  loutput->methods = &svg_draw_methods;
  static_replay(loutput, filename, replay);
}
#endif /* CAIRO_HAS_SVG_SURFACE */
