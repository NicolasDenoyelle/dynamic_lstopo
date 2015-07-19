#include <errno.h>
#include <assert.h>
#include <papi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>
#include <math.h>
#include <float.h>

#include "monitor_utils.h"

#define M_alloc(pointer,n_elem,size)	\
  if((pointer=calloc(n_elem,size))==NULL){		\
    perror("calloc failed");				\
    exit(EXIT_FAILURE);					\
  }							

#define Monitors_get_monitor_node(m,index,sibling) ((struct monitor_node*)(hwloc_get_obj_by_depth(m->topology,m->depths[index],sibling)->userdata))
#define Monitors_get_level_max(m,i) m->max[i]
#define Monitors_get_level_min(m,i) m->min[i]
#define Monitors_get_monitor_value(m, monitor_idx, sibling_idx) Monitors_get_monitor_node(m,monitor_idx,sibling_idx)->val

static inline void zerof(double * array, unsigned int size){
  unsigned int i = size;
  for(i=0;i<size;i++) array[i]=0;
}

static inline void zerod(long long * array, unsigned int size){
  unsigned int i = size;
  for(i=0;i<size;i++) array[i]=0;
}

static struct monitor_node *
new_monitor_node(unsigned int n_events, unsigned depth, unsigned sibling, unsigned id)
{
  struct monitor_node * out = malloc(sizeof(*out));
  if(out==NULL){
    perror("malloc failed\n");
    exit(EXIT_FAILURE);
  }

  M_alloc(out->counters_val,n_events,sizeof(long long));
  zerod(out->counters_val,n_events);

  pthread_mutex_init(&(out->read_lock), NULL);
  pthread_mutex_init(&(out->update_lock), NULL);
  out->real_usec=0;
  out->old_usec=0;
  out->uptodate=0;
  out->val = out->val1 = out->val2 = 0;
  out->depth = depth;
  out->sibling = sibling;
  out->id = id;
  out->userdata=NULL;
  return out;
}

static void
delete_monitor_node(struct monitor_node * out)
{
  if(out==NULL)
    return;
  free(out->counters_val);
  pthread_mutex_destroy(&(out->read_lock));
  pthread_mutex_destroy(&(out->update_lock));
  free(out);
}

/***************************************************************************************************************/
/*                                                   PRIVATE                                                   */
/***************************************************************************************************************/

double no_fun     (long long * in){return 0;}
double default_fun(long long * in){return (double)in[0];}

void
handle_error(int err)
{
  if(err!=0)
    fprintf(stderr,"PAPI error %d: ",err);
  switch(err){
  case PAPI_EINVAL:
    fprintf(stderr,"Invalid argument.\n");
    break;
  case PAPI_ENOINIT:
    fprintf(stderr,"PAPI library is not initialized.\n");
    break;
  case PAPI_ENOMEM:
    fprintf(stderr,"Insufficient memory.\n");
    break;
  case PAPI_EISRUN:
    fprintf(stderr,"Eventset is already_counting events.\n");
    break;
  case PAPI_ECNFLCT:
    fprintf(stderr,"This event cannot be counted simultaneously with another event in the monitor eventset.\n");
    break;
  case PAPI_ENOEVNT:
    fprintf(stderr,"This event is not available on the underlying hardware.\n");
    break;
  case PAPI_ESYS:
    fprintf(stderr, "A system or C library call failed inside PAPI, errno:%s\n",strerror(errno)); 
    break;
  case PAPI_ENOEVST:
    fprintf(stderr,"The EventSet specified does not exist.\n");
    break;
  case PAPI_ECMP:
    fprintf(stderr,"This component does not support the underlying hardware.\n");
    break;
  case PAPI_ENOCMP:
    fprintf(stderr,"Argument is not a valid component. PAPI_ENOCMP\n");
    break;
  case PAPI_EBUG:
    fprintf(stderr,"Internal error, please send mail to the developers.\n");
    break;
  default:
    fprintf(stderr,"Unknown error ID, sometimes this error is due to \"/proc/sys/kernel/perf_event_paranoid\" not set to -1\n");
    break;
  }
}

static int 
init_eventset(monitors_t m, int * eventset, unsigned cpu_num, int print_err)
{
  int err; unsigned int i;
  PAPI_event_info_t info;

  /* initialize eventset */
  *(eventset)=PAPI_NULL;
  if((err=PAPI_create_eventset(eventset))!=PAPI_OK){
    if(print_err){
      fprintf(stderr,"eventset init: create failed\n");
      handle_error(err);
    }
    return 1;
  }

  if((err = PAPI_assign_eventset_component(*eventset, 0)) != PAPI_OK){
    if(print_err)
      handle_error(err);
    int code;
    PAPI_event_name_to_code(m->event_names[0],&code);
    PAPI_get_event_info(code,&info);
    if(print_err)
      fprintf(stderr,"%s component = %d\n",m->event_names[0],info.component_index);
  }


  PAPI_option_t cpu_option;
  cpu_option.cpu.eventset=*eventset;
  cpu_option.cpu.cpu_num = cpu_num;

  if((err = PAPI_set_opt(PAPI_CPU_ATTACH,&cpu_option))!=PAPI_OK  && print_err){
    fprintf(stderr, "Could not set PAPI option PAPI_CPU_ATTACH to %u\n",cpu_num);
    handle_error(err);
  }

  for(i=0;i<m->n_events;i++){
    err =  PAPI_add_named_event(*(eventset),m->event_names[i]);
    if(err!=PAPI_OK){
      if(print_err){
	fprintf(stderr,"event set init: could not add \"%s\" to eventset.\n",m->event_names[i]);
	handle_error(err);
      }
      return 1;
    }
  }

  return 0;
}

static monitors_t 
new_Monitors(hwloc_topology_t topology, unsigned int n_events, char ** event_names, 
	     const char * output, int accum)
{
  unsigned int i, depth; 
  monitors_t m;

  assert(hwloc_get_api_version()==HWLOC_API_VERSION);
  if(event_names==NULL){
    return NULL;
  }

  m = malloc(sizeof(struct monitors));
  if(m==NULL){
    perror("malloc failed\n");
    return NULL;
  }
  m->accum=accum;
  m->n_events=0;
  m->event_names=NULL;
  m->n_PU=0;
  m->pthreads=NULL;
  m->count=0;
  m->names=NULL;
  m->depths=NULL;
  m->depth_names=NULL;
  m->output_fd=0;
  m->pw=NULL;
  m->topology = NULL;
  m->dlhandle=NULL;
  m->phase=0;
  m->n_nodes=0;

  /* initialize topology */
  if(topology!=NULL){
    hwloc_topology_dup(&m->topology,topology);
  }
  else{
    topology_init(&m->topology);
  }

  /* Initialize output */
  if(output==NULL){
    m->output_fd=open("/dev/null",O_WRONLY | O_NONBLOCK, S_IRUSR|S_IWUSR);
  }
  else{
    m->output_fd=open(output,O_WRONLY | O_NONBLOCK | O_CREAT, S_IRUSR|S_IWUSR);
    if(m->output_fd==-1){
      fprintf(stderr,"warning: could not open or create %s:\n",output);
      fprintf(stderr,"%s\n",strerror(errno));
      m->output_fd=STDIN_FILENO;
    }
  }

  /* set mutex for reading and printing counters */
  pthread_mutex_init(&m->print_mtx,NULL);
  /* count core number */
  depth = hwloc_topology_get_depth(m->topology);
  m->n_PU = hwloc_get_nbobjs_by_depth(m->topology, depth-1);
  pthread_barrier_init(&m->barrier,NULL,m->n_PU+1);
  m->allocated_count=4;

  M_alloc(m->pthreads,m->n_PU,sizeof(pthread_t));
  M_alloc(m->depths,m->allocated_count,sizeof(int));
  M_alloc(m->depth_names,m->allocated_count,sizeof(char*));
  M_alloc(m->compute,m->allocated_count,sizeof(double (*)(long long*)));
  M_alloc(m->min,m->allocated_count,sizeof(double));
  M_alloc(m->max,m->allocated_count,sizeof(double));
  M_alloc(m->logscale,m->allocated_count,sizeof(int));
  M_alloc(m->names,m->allocated_count,sizeof(char *));
  M_alloc(m->event_names,n_events,sizeof(char*));
  M_alloc(m->PU_vals,m->n_PU,sizeof(struct monitor_node *));

  for(i=0;i<m->n_PU;i++){
    m->pthreads[i]=0;
    m->PU_vals[i] = new_monitor_node(n_events,depth-1,i,(unsigned)-1);
  }

  for(i=0;i<m->allocated_count;i++){
    m->depths[i]=0;
    m->min[i] = DBL_MAX;
    m->max[i] = DBL_MIN;
    m->logscale[i] = 1;
  }
  /* allocate event names and eventset*/
  m->n_events = n_events;
  for(i=0;i<m->n_events;i++){
    m->event_names[i]=strdup(event_names[i]);
  }
  return m;
}

static int 
add_Monitor(monitors_t m, const char * name, char * hwloc_obj_name, 
	    double (*fun)(long long *), double max, double min, int logscale)
{
  unsigned int i,j, n_obj;
  hwloc_obj_t node;
  if(m==NULL)
    return 1;

  /* realloc necessary space */
  if(m->allocated_count<=m->count){
    m->allocated_count*=2;
    if((m->names=realloc(m->names, sizeof(char*)*m->allocated_count))==NULL)
      exit(EXIT_FAILURE);
    if((m->depths=realloc(m->depths, sizeof(int)*m->allocated_count))==NULL)
      exit(EXIT_FAILURE);
    if((m->depth_names=realloc(m->depth_names, sizeof(char*)*m->allocated_count))==NULL)
      exit(EXIT_FAILURE);
    if((m->max=realloc(m->max, sizeof(int)*m->allocated_count))==NULL)
      exit(EXIT_FAILURE);
    if((m->min=realloc(m->min, sizeof(int)*m->allocated_count))==NULL)
      exit(EXIT_FAILURE);
    if((m->compute=realloc(m->compute, sizeof(double(*)(long long*))*m->allocated_count))==NULL)
      exit(EXIT_FAILURE);
  }

  /* find hwloc obj depth */
  int depth = hwloc_get_obj_depth_by_name(m->topology, hwloc_obj_name);
  if(depth==-1)
    return 1;

  i=0;
  while (i<m->count && m->depths[i] < depth ){i++;}
  if(m->depths[i]==depth){
    fprintf(stderr,"cannot insert monitor %s at depth %u because another monitor already exists at this depth\n",name,depth);
    return 1;
  }

  /* insert sorted */
  for(j=m->count;j>i;j--){
    m->names[j] = m->names[j-1];
    m->depths[j] = m->depths[j-1];
    m->depth_names[j] = m->depth_names[j-1];
    m->max[j] = m->max[j-1];
    m->min[j] = m->min[j-1];
    m->logscale[j] = m->logscale[j-1];
    m->compute[j] = m->compute[j-1];
  }

  m->names[i] = strdup(name);
  m->depths[i] = depth;
  m->depth_names[i] = strdup(hwloc_obj_name);
  m->compute[i] = fun;
  m->min[j] = min;
  m->max[j] = max;
  m->logscale[i] = logscale;
  m->count++;

  n_obj=hwloc_get_nbobjs_by_depth(m->topology,depth);
  if(n_obj==0){
    fprintf(stderr,"No object in topology at depth %u\n",depth);
    return 1;
  }

  while(n_obj--){
    node=hwloc_get_obj_by_depth(m->topology,depth,n_obj);
    node->userdata = new_monitor_node(m->n_events,depth,n_obj,m->n_nodes);
    m->n_nodes++;
  }  
  return 0;
}

struct thread_junk{
  int * eventset;
  hwloc_bitmap_t active;
  long long *values;
  long long *old_values;
};

void monitors_thread_stop(void * thread_junk){
  if(thread_junk==NULL) return;
  struct thread_junk * tj = (struct thread_junk *)thread_junk;
  PAPI_destroy_eventset(tj->eventset);
  hwloc_bitmap_free(tj->active);
  free(tj->values);
  free(tj->old_values);
}


void * monitors_thread(void* monitors){
  monitors_t m;
  pthread_t tid;
  unsigned int i,j,weight, nobj;
  int tidx, p_tidx, eventset, depth;
  hwloc_obj_t obj,cpu;
  struct monitor_node * out, * PU_vals;
  struct value_line output;
  char type[10];
  if(monitors==NULL)
    pthread_exit(NULL);

  /**************** Thread init ****************/
  m = (monitors_t)monitors;
  weight=0; tidx=-1; eventset=PAPI_NULL; depth=hwloc_topology_get_depth(m->topology)-1;
  tid = pthread_self();
  for(i=0;i<m->n_PU;i++){
    if(m->pthreads[i]==tid){
      tidx=i;
      break;
    }
  }  

  /* stores time and difference between two samples*/
  PU_vals = m->PU_vals[tidx];
  
  /* bind my self to tidx core */
  cpu = hwloc_get_obj_by_depth(m->topology,depth,tidx);
  hwloc_bitmap_t active_pu = hwloc_bitmap_dup(cpu->cpuset);
  hwloc_bitmap_singlify(active_pu);
  hwloc_set_thread_cpubind(m->topology,tid, active_pu, HWLOC_CPUBIND_THREAD | HWLOC_CPUBIND_STRICT);
  p_tidx = hwloc_bitmap_first(active_pu);
  
  /* Initialize my eventset */
  if(init_eventset(m,&eventset,p_tidx,1)!=0){
    fprintf(stderr,"%d failed to init its eventset\n",tidx);
    exit(EXIT_FAILURE);
  }

  /* initialize counters value container */
  long long * values, *old_values , *tmp;
  M_alloc(values,    m->n_events,sizeof(long long));
  M_alloc(old_values,m->n_events,sizeof(long long));
  /* values to check fast event locations */
  int events_location[m->n_events];
  
  PAPI_event_info_t info;
  int eventcode;
  for(i=0;i<m->n_events;i++){
    values[i]=0; 
    old_values[i]=0;
    PAPI_event_name_to_code(m->event_names[i],&eventcode);
    PAPI_get_event_info(eventcode,&info);
    events_location[i]=info.location;    
  } 
  int core_depth    = hwloc_get_type_depth(m->topology, HWLOC_OBJ_CORE);
  int machine_depth = hwloc_get_type_depth(m->topology, HWLOC_OBJ_MACHINE);
  int package_depth = hwloc_get_type_depth(m->topology, HWLOC_OBJ_SOCKET);

  /* fill the structure of objects to free at cleanup */
  struct thread_junk tj;
  tj.eventset = &eventset;
  tj.active = active_pu;
  tj.values = values;
  tj.old_values = old_values;
  pthread_cleanup_push(monitors_thread_stop,&tj);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
  

  if(m->pw==NULL)
    PAPI_start(eventset);

  /**************** Thread actual work ****************/
  for(;;){
    /* signal we are ready for new update*/
  next_loop:;
    pthread_barrier_wait(&(m->barrier));

    /* update time stamp*/
    PU_vals->old_usec=PU_vals->real_usec;
    PU_vals->real_usec=PAPI_get_real_usec();
    /* check actually sampling PUs */
    if(m->pw!=NULL){
      /* A pid is specified */
      if(proc_watch_check_start_pu(m->pw,p_tidx)){
	PAPI_start(eventset);
      }
      else if(proc_watch_check_stop_pu(m->pw,p_tidx)){
	PAPI_stop(eventset,PU_vals->counters_val);
	goto next_loop;
      }
      else if(!proc_watch_get_pu_state(m->pw,p_tidx)){
	goto next_loop;
      }
    }
    /* gathers counters */
    PAPI_read(eventset,values);
    /* calculate difference to get total counter variation between samples */
    for(i=0;i<m->n_events;i++){
      /* overflow ? */
      if(values[i]>=old_values[i])
	PU_vals->counters_val[i] = values[i]-old_values[i];
    }
    /* swap values and old_values for next loop */
    tmp = values;
    values = old_values;
    old_values = tmp;
    /* reduce counters for every monitors_t */
    for(i=0;i<m->count;i++){
      obj = hwloc_get_ancestor_obj_by_depth(m->topology,m->depths[i],cpu);
      out = (struct monitor_node *)(obj->userdata);

      /* get the number of threads updating this array of counters */
      if(m->pw!=NULL){
	proc_watch_get_watched_in_cpuset(m->pw,obj->cpuset,active_pu);
	weight = hwloc_bitmap_weight(active_pu);
      } else weight = hwloc_bitmap_weight(obj->cpuset);

      pthread_mutex_trylock(&(out->update_lock));
      pthread_mutex_lock(&(out->read_lock));
      /* i am the first to acquire the lock, i reset values */
      if(out->uptodate==0){
	out->old_usec = out->real_usec;
	out->real_usec = 0;
	if(!m->accum){
	  zerod(out->counters_val,m->n_events);
	}
      }

      /* accumulate values if necessary */
      for(j=0;j<m->n_events;j++){
	switch(events_location[j]){
	case PAPI_LOCATION_UNCORE:
	  if(obj->depth < core_depth)
	    out->counters_val[j] = PU_vals->counters_val[j];
	  else
	    out->counters_val[j] += PU_vals->counters_val[j];
	  break;      
	case PAPI_LOCATION_CPU:
	  if(obj->depth >= machine_depth)
	    out->counters_val[j] = PU_vals->counters_val[j];
	  else
	    out->counters_val[j] += PU_vals->counters_val[j];
	  break;
	case PAPI_LOCATION_PACKAGE:
	  if(obj->depth >= package_depth)
	    out->counters_val[j] = PU_vals->counters_val[j];
	  else
	    out->counters_val[j] += PU_vals->counters_val[j];
	  break;
	default:
	  out->counters_val[j]+=PU_vals->counters_val[j];
	  break;
	}
      }

      out->real_usec += PU_vals->real_usec;
      out->uptodate++;
      /* I am the last to update value on current node, i update monitor value */
      if(out->uptodate==weight){
	out->uptodate=0;
	out->val2 = out->val1;
	out->val1 = out->val;
	out->val = m->compute[i](out->counters_val);
	/* compute real_usec mean value */
	out->real_usec/=weight;
	output.real_usec=out->real_usec;
	output.phase=(m->phase);
	output.value = out->val;
	output.id = out->id;
	pthread_mutex_lock(&(m->print_mtx));
	/* update min and max value at depth i*/
	m->max[i] = out->val > m->max[i] ? out->val : m->max[i];
	m->min[i] = out->val < m->min[i] ? out->val : m->min[i];
	/* print to output_file */
	output_line_content_paje(m->output_fd,&output);
	pthread_mutex_unlock(&(m->print_mtx));
	pthread_mutex_unlock(&(out->read_lock));
	pthread_mutex_unlock(&(out->update_lock));
      }
      else{
	pthread_mutex_unlock(&(out->read_lock));
      }
    }
  }
  pthread_cleanup_pop(0);
  return NULL;
}

static int
chk_monitors_lib(const char * libso_path, const char * perf_group_filename)
{
  struct stat stat_group, stat_lib;

  if(perf_group_filename==NULL)
    return 0;
  
  /* no perf group defined */
  if(access(perf_group_filename, R_OK ) == -1 ){
    return 0;
  }
  
  /* no perf group library */
  if(access(libso_path, R_OK ) == -1){
    return 0;
  }
  
  /* perf group is more recent than library */
  if(stat(perf_group_filename,&stat_group) == -1 || stat(libso_path, &stat_lib)==-1)
    return 0;
  if(difftime(stat_group.st_mtime,stat_lib.st_mtime)>0)
    return 0;
  
  /* lib is ready for use */
  return 1;
}

void unload_monitors_lib(monitors_t m){
  if(m->dlhandle==NULL)
    return;
  dlclose(m->dlhandle);
}


/***************************************************************************************************************/
/*                                                   PUBLIC                                                    */
/***************************************************************************************************************/
monitors_t
new_default_Monitors(hwloc_topology_t topology, const char * output, int accum)
{
  unsigned i,depth = hwloc_topology_get_depth(topology)-1;
  int eventset = PAPI_NULL;

  PAPI_library_init( PAPI_VER_CURRENT);
  PAPI_create_eventset(&eventset);

  unsigned count=0, max_count = PAPI_MAX_HWCTRS + PAPI_MAX_PRESET_EVENTS;
  char ** event_names = malloc(sizeof(char*)*max_count);
  char ** obj_names = malloc(sizeof(char*)*max_count);
  char obj_name[20];
  int event_code = 0 | PAPI_PRESET_MASK;
  PAPI_event_info_t info;

  PAPI_enum_event( &event_code, PAPI_ENUM_FIRST );
  do {
    hwloc_obj_type_snprintf(obj_name,20,hwloc_get_obj_by_depth(topology,depth,0),0);
    if ( PAPI_get_event_info( event_code, &info ) == PAPI_OK &&
	 PAPI_add_named_event(eventset,info.symbol) == PAPI_OK
	 && strcmp(obj_name,"Package") && strcmp(obj_name,"Socket")){
      obj_names[count] = strdup(obj_name);
      event_names[count]=strdup(info.symbol);
      count++;
    }
  } while (depth-- && PAPI_enum_event( &event_code, PAPI_PRESET_ENUM_AVAIL ) == PAPI_OK);
  
  PAPI_destroy_eventset(&eventset);

  monitors_t m;
  m = new_Monitors(topology, count, event_names, output, accum);
  if(m==NULL){
    fprintf(stderr,"default monitors_t creation failed\n");
    for(i=0;i<count;i++){
      free(event_names[i]);
      free(obj_names[i]);
    }
    free(obj_names);
    free(event_names);
    return NULL;
  }
  for(i=0;i<count;i++){
    add_Monitor(m,event_names[i],obj_names[i],default_fun,DBL_MIN,DBL_MAX,1);
    free(event_names[i]);
    free(obj_names[i]);
  }
  free(obj_names);
  free(event_names);
  return m;
}


monitors_t
load_Monitors_from_config(hwloc_topology_t topology, const char * perf_group_file, const char * output, int accum)
{
  struct parsed_names * pn;
  monitors_t m;
  unsigned int i;
  double (*fun)(long long*);
  void * dlhandle;

  if(!chk_input_file(perf_group_file))
    return NULL;

  pn = parser(perf_group_file);

  /* load shared libraries */
  dlerror();
  dlhandle = dlopen(pn->libso_path,RTLD_NOW);
  if(dlhandle==NULL){
    fprintf(stderr,"loading error:%s\n",dlerror());
    return NULL;
  }  
  dlerror();

  m = new_Monitors(topology, pn->n_events,pn->event_names,output, accum);
  m->libsopath = strdup(pn->libso_path);
  m->dlhandle=dlhandle;

  for(i=0;i<pn->n_events;i++)
    free(pn->event_names[i]);
  
  for(i=0;i<pn->n_monitors;i++){
    fun = dlsym(m->dlhandle,(pn->monitor_names)[i]);
    if(fun==NULL){
      fprintf(stderr,"could not load monitor function %s : %s\n",(pn->monitor_names)[i],dlerror());
      add_Monitor(m,(pn->monitor_names)[i],(pn->monitor_obj)[i],no_fun, pn->max[i], pn->min[i], pn->logscale[i]);
    }
    else
      add_Monitor(m,(pn->monitor_names)[i],(pn->monitor_obj)[i],*fun,pn->max[i], pn->min[i], pn->logscale[i]);
    
    free(pn->monitor_names[i]);
    free(pn->monitor_obj[i]);
  }
  
  free(pn->monitor_names);
  free(pn->event_names);
  free(pn->min);
  free(pn->max);
  free(pn->logscale);
  free(pn->monitor_obj);
  free(pn->libso_path);
  free(pn);
  //print_monitors(m);
  return m;
}

unsigned int 
Monitors_watch_pid(monitors_t m,unsigned int pid)
{
  unsigned int ret=0;
  if(m->pw!=NULL){
    ret = proc_watch_get_pid(m->pw);
    delete_proc_watch(m->pw);
  }
  if(pid!=0)
    m->pw = new_proc_watch(pid);
  if(m->pw==NULL)
    return -1;
  return ret;
}

int
Monitors_start(monitors_t m)
{
 unsigned int i; int err;
 err = PAPI_is_initialized();
 if (err != PAPI_LOW_LEVEL_INITED){
   err = PAPI_library_init(PAPI_VER_CURRENT);
   if (err != PAPI_VER_CURRENT && err > 0) {
     fprintf(stderr,"PAPI library version mismatch!\en");
      exit(EXIT_FAILURE); 
   }
   if((err=PAPI_is_initialized())!=PAPI_LOW_LEVEL_INITED){
     fprintf(stderr,"PAPI library init failed!\n");
     exit(EXIT_FAILURE);
   }
 }

 PAPI_thread_init(pthread_self);
 
 output_header_paje(m);
 for(i=0;i<m->n_PU;i++){
 err=pthread_create(&(m->pthreads[i]),NULL,monitors_thread,(void*)m);
 if(err!=0){
   fprintf(stderr,"pthread create failed with err: %d\n",err);
   exit(EXIT_FAILURE);
 }
 }
 Monitors_update_counters(m);
 return 0;
}

void
Monitors_update_counters(monitors_t m){
  pthread_barrier_wait(&(m->barrier));
}

inline void         
Monitors_set_phase(monitors_t m, unsigned phase)
{
  m->phase = phase;
}

inline long long
Monitors_get_counter_value(monitors_t m,unsigned int counter_idx,unsigned int PU_idx)
{
  return Monitors_get_monitor_node(m,hwloc_topology_get_depth(m->topology)-1,PU_idx)->counters_val[counter_idx];
}

inline double 
Monitors_get_monitor_variation(monitors_t m, unsigned depth, unsigned int sibling_idx)
{ 
  hwloc_obj_t obj = hwloc_get_obj_by_depth(m->topology,depth,sibling_idx);
  return ((struct monitor_node *)(obj->userdata))->val-((struct monitor_node *)(obj->userdata))->val1;
}


double
Monitors_wait_monitor_value(monitors_t m, unsigned depth, unsigned int sibling_idx)
{
  hwloc_obj_t obj = hwloc_get_obj_by_depth(m->topology,depth,sibling_idx);
  struct monitor_node * box = obj->userdata;
  double val;
  pthread_mutex_lock(&(box->update_lock));
  val = box->val;
  pthread_mutex_unlock(&(box->update_lock));
  return val;
}

double
Monitors_wait_monitor_variation(monitors_t m, unsigned depth, unsigned int sibling_idx)
{
  hwloc_obj_t obj = hwloc_get_obj_by_depth(m->topology,depth,sibling_idx);
  struct monitor_node * box = obj->userdata;
  double val;
  pthread_mutex_lock(&(box->update_lock));
  val = box->val - box->val1;
  pthread_mutex_unlock(&(box->update_lock));
  return val;
}

void
delete_Monitors(monitors_t m)
{
  unsigned int i,j;
  int n_obj;

  if(m==NULL)
    return;

  for(i=0;i<m->n_PU;i++)
    pthread_cancel(m->pthreads[i]);

  /* for(i=0;i<m->n_PU;i++){ */
  /*   pthread_join(m->pthreads[i],NULL); */
  /* } */

  pthread_mutex_destroy(&m->print_mtx);
  pthread_barrier_destroy(&(m->barrier));
  free(m->pthreads);

  for(i=0;i<m->count;i++){
    free(m->depth_names[i]);
    free(m->names[i]);
    n_obj=hwloc_get_nbobjs_by_depth(m->topology,m->depths[i]);
    for(j=0;j<n_obj;j++){
      delete_monitor_node(Monitors_get_monitor_node(m,i,j));
    }
  }
  for(i=0;i<m->n_PU;i++){
    delete_monitor_node(m->PU_vals[i]);
  }

  free(m->PU_vals);
  free(m->compute);
  free(m->depths);
  free(m->max);
  free(m->min);
  free(m->names);
  free(m->depth_names);

  for(i=0;i<m->n_events;i++){
    free(m->event_names[i]);
  }
  free(m->event_names);
  if(m->pw!=NULL)
    delete_proc_watch(m->pw);
  unload_monitors_lib(m);
  remove(m->libsopath);
  free(m);
  // hwloc_topology_destroy(m->topology);
  PAPI_shutdown();
}

void print_monitors(monitors_t m)
{
  if(!m)
    return;
  hwloc_bitmap_t watched_cpu = hwloc_bitmap_dup(hwloc_topology_get_complete_cpuset(m->topology));
  proc_watch_get_watched_in_cpuset(m->pw,watched_cpu,watched_cpu);
  if(m->libsopath)
    printf("Functions from: %s\n",m->libsopath);
  else
    printf("Default monitors\n");
  unsigned i;
  printf("%u Events watched:\n",m->n_events);
  for(i=0;i<m->n_events;i++)
    printf("\t%s\n",m->event_names[i]);
  printf("%u Monitors:\n",m->count);
  for(i=0;i<m->count;i++)
    printf("%10s:%s\n",m->depth_names[i],m->names[i]);
}

