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
#include "pwatch.h"
#include "parser.h"
#include "monitor.h"

struct node_box{
  long long *     counters_val;
  double          val, old_val;
  long long       real_usec, old_usec;
  int             uptodate;
  pthread_mutex_t read_lock;
  pthread_mutex_t update_lock;
};

struct monitors{
  /**
   * In hwloc topology tree at each monitor depth, every sibling stores a struct node_box in its userdata
   */
  hwloc_topology_t  topology;

  /**
   * When -p option is used, the structure holds in a PU array pid childrens task id; 
   *
   */
  struct proc_watch *pw;

  int               output_fd; 

  int               n_events;    
  char        **    event_names;       /* n_events * strlen(event_name) */

  unsigned int      n_PU;
  pthread_t       * pthreads;          /* nb_PU */
  struct node_box ** PU_vals;           /* nb_PU */

  pthread_barrier_t barrier;

  pthread_cond_t    cond;
  pthread_mutex_t   cond_mtx;

  unsigned int      uptodate;

  unsigned int      count;             /* nb_monitors */
  unsigned int      allocated_count; 
  char        **    names;             /* nb_monitors */
  double  (**compute)(long long *);    /* nb_monitors */
  unsigned int    * depths;            /* nb_monitors */
  double          * max, * min;        /* nb_monitors */
 };


#define M_alloc(obj,free_obj,pointer,n_elem,size)	\
  if((pointer=calloc(n_elem,size))==NULL){		\
    perror("calloc failed");				\
    free_obj(obj);					\
    return NULL;					\
  }							


inline void zerof(double * array, unsigned int size){
  unsigned int i = size;
  for(i=0;i<size;i++) array[i]=0;
}

inline void zerod(long long * array, unsigned int size){
  unsigned int i = size;
  for(i=0;i<size;i++) array[i]=0;
}

struct node_box *
new_counters_output(unsigned int n_events)
{
  struct node_box * out = malloc(sizeof(*out));
  if(out==NULL){
    perror("malloc failed\n");
    return NULL;
  }
  out->counters_val = calloc(n_events,sizeof(long long));
  zerod(out->counters_val,n_events);
  pthread_mutex_init(&(out->read_lock),NULL);
  pthread_mutex_init(&(out->update_lock),NULL);
  out->real_usec=0;
  out->old_usec=0;
  out->uptodate=0;
  out->val=0;
  out->old_val=0;
  return out;
}

void
delete_counters_output(struct node_box * out)
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

double no_fun(long long * in){return 0;}
double flop_fp(long long * in){return (double)(in[0]*1.0);}
double L1d_access_per_fpops(long long * in){return (double)(in[0]==0?-1:in[1]/in[0]);}
int  compareint(const void* a,const void* b ){return *(int*)a - *(int*)b;}

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
    fprintf(stderr,"This component does not support the underlying hardware.");
    break;
  default:
    break;
  }
}

int 
init_eventset(int * eventset,unsigned int n_events,  char** event_names)
{
  int err; unsigned int i;
  *(eventset)=PAPI_NULL;
  if((err=PAPI_create_eventset(eventset))!=PAPI_OK){
    fprintf(stderr,"eventset init: create failed\n");
    handle_error(err);
    return 1;
  }
  if((err = PAPI_assign_eventset_component(*eventset, 0)) != PAPI_OK) handle_error(err);
  if((err = PAPI_set_multiplex(*eventset))                != PAPI_OK) handle_error(err);
  for(i=0;i<n_events;i++){
    err =  PAPI_add_named_event(*(eventset),event_names[i]);
    if(err!=PAPI_OK){
      fprintf(stderr,"event set init: could not add \"%s\" to eventset.\n",event_names[i]);
      handle_error(err);
      return 1;
    }
  }
  PAPI_granularity_option_t opt; 
  opt.eventset=*(eventset);
  opt.granularity=PAPI_GRN_SYS;
  err=PAPI_set_opt(PAPI_GRANUL,(PAPI_option_t *)(&opt));
  if(err!=PAPI_OK){
    fprintf(stderr,"eventset init: cannot set eventset granularity. errorcode:%d\n",err);
  }
  return 0;
}

Monitors_t new_Monitors(hwloc_topology_t topology, 
			unsigned int n_events, 
			char ** event_names, 
			const char * output, 
			unsigned int pid){
  assert(hwloc_get_api_version()==HWLOC_API_VERSION);
  if(event_names==NULL || topology==NULL){
    return NULL;
  }
  unsigned int i; int err;

  err = PAPI_is_initialized();
  if (err != PAPI_LOW_LEVEL_INITED){
    err = PAPI_library_init(PAPI_VER_CURRENT);
    if (err != PAPI_VER_CURRENT && err > 0) {
      fprintf(stderr,"PAPI library version mismatch!\en");
      exit(1); 
    }
    if(PAPI_is_initialized()!=PAPI_LOW_LEVEL_INITED){
      fprintf(stderr,"PAPI library init failed!\n");
      exit(1);
    }
    PAPI_thread_init(pthread_self);
    PAPI_multiplex_init();
  }

  /* try to initialize an individual eventset to check counters availability */
  int eventset;
  if(init_eventset(&eventset,n_events,event_names)!=0){
    return NULL;
  }
  PAPI_destroy_eventset(&eventset);

  Monitors_t m = malloc(sizeof(struct monitors));
  if(m==NULL){
    perror("malloc failed\n");
    return NULL;
  }
  m->n_events=0;
  m->event_names=NULL;
  m->n_PU=0;
  m->pthreads=NULL;
  m->count=0;
  m->allocated_count=0;
  m->uptodate=0;
  m->names=NULL;
  m->depths=NULL;
  m->output_fd=0;
  m->pw=NULL;
  m->topology = topology;

  /* Initialize output */
  if(output==NULL)
    m->output_fd=1;
  else{
    m->output_fd=open(output,O_WRONLY | O_NONBLOCK);
    if(m->output_fd==-1){
      fprintf(stderr,"warning: could not open or create %s:\n",output);
      fprintf(stderr,"%s\n",strerror(errno));
      m->output_fd=1;
    }
  }

  /* set condition for reading counters */
  pthread_mutex_init(&m->cond_mtx,NULL);
  if((err=pthread_cond_init(&(m->cond),NULL))!=0){
    fprintf(stderr,"failed to initialize mutex\n");
    return NULL;
  }

  /* count core number */
  int depth = hwloc_topology_get_depth(m->topology);
  m->n_PU = hwloc_get_nbobjs_by_depth(m->topology, depth-1);
  pthread_barrier_init(&m->barrier,NULL,m->n_PU);
  if(pid!=0)
    m->pw = new_proc_watch(m->topology, pid, m->n_PU);
  m->count=0;
  m->allocated_count=4;

  M_alloc(m,delete_Monitors,m->pthreads,m->n_PU,sizeof(pthread_t));
  M_alloc(m,delete_Monitors,m->depths,m->allocated_count,sizeof(int));
  M_alloc(m,delete_Monitors,m->max,m->allocated_count,sizeof(double));
  M_alloc(m,delete_Monitors,m->min,m->allocated_count,sizeof(double));
  M_alloc(m,delete_Monitors,m->compute,m->allocated_count,sizeof(double (*)(long long*)));
  M_alloc(m,delete_Monitors,m->names,m->allocated_count,sizeof(char *));
  M_alloc(m,delete_Monitors,m->event_names,n_events,sizeof(char*));
  M_alloc(m,delete_Monitors,m->PU_vals,m->n_PU,sizeof(struct node_box *));

  zerof(m->max,m->allocated_count);
  zerof(m->min,m->allocated_count);

  for(i=0;i<m->n_PU;i++){
    m->pthreads[i]=0;
    m->PU_vals[i] = new_counters_output(n_events);
  }

  for(i=0;i<m->allocated_count;i++)
    m->depths[i]=0;

  /* allocate event names and eventset*/
  m->n_events = n_events;
  for(i=0;i<m->n_events;i++){
    m->event_names[i]=strdup(event_names[i]);
    if(strcmp(m->event_names[i],event_names[i])){
      fprintf(stderr,"strdup_failed: %s -> %s\n",m->event_names[i], event_names[i]);
    }
  }
  return m;
}

int add_Monitor(Monitors_t m, const char * name, unsigned int depth, double (*fun)(long long *)){
  if(m==NULL)
    return 1;
  if(m->allocated_count<=m->count){
    m->allocated_count*=2;
    if((m->names=realloc(m->names, sizeof(char*)*m->allocated_count))==NULL)
      return 1;
    if((m->depths=realloc(m->depths, sizeof(int)*m->allocated_count))==NULL)
      return 1;
    if((m->compute=realloc(m->compute, sizeof(double(*)(long long*))*m->allocated_count))==NULL)
      return 1;
    if((m->min=realloc(m->min, sizeof(double)*m->allocated_count))==NULL)
      return 1;
    if((m->max=realloc(m->max, sizeof(double)*m->allocated_count))==NULL)
      return 1;
    zerof(&(m->max[m->count]),m->allocated_count-m->count);
    zerof(&(m->min[m->count]),m->allocated_count-m->count);
  }

  int i;
  for(i=0;i<m->count;i++){
    if(m->depths[i]==depth){
      fprintf(stderr,"cannot insert monitor %s at depth %d because another monitor already exists at this depth\n",name,depth);
      return 1;
    }
  }

  hwloc_obj_t node;
  int n_obj=hwloc_get_nbobjs_by_depth(m->topology,depth);
  if(n_obj==0){
    fprintf(stderr,"No object in topology at depth %d\n",depth);
    return 1;
  }

  while(n_obj--){
    node=hwloc_get_obj_by_depth(m->topology,depth,n_obj);
    node->name=strdup(name);
    node->userdata = new_counters_output(m->n_events);
  }
  
  m->names[m->count] = strdup(name);
  m->depths[m->count] = depth;
  m->compute[m->count] = fun;
  m->count++;
  return 0;
}

void Monitors_thread_stop(void * eventset){
  if( eventset==NULL) return;
  PAPI_destroy_eventset((int*)eventset);
}


void * Monitors_thread(void* monitors){
  if(monitors==NULL)
    pthread_exit(NULL);
  Monitors_t m = (Monitors_t)monitors;
  pthread_t tid = pthread_self();
  unsigned int i,j;
  int weight=0, tidx=-1, eventset=PAPI_NULL, depth=hwloc_topology_get_depth(m->topology)-1;
  for(i=0;i<m->n_PU;i++){
    if(m->pthreads[i]==tid){
      tidx=i;
      break;
    }
  }  
  /* bind my self to tidx core */
  hwloc_obj_t obj,cpu = hwloc_get_obj_by_depth(m->topology,depth,tidx);
  if(cpu==NULL || cpu->cpuset==NULL){
    fprintf(stderr,"obj[%d] at depth %d is null\n",tidx,depth);
    exit(1);
  }
  hwloc_set_cpubind(m->topology, cpu->cpuset, HWLOC_CPUBIND_STRICT|HWLOC_CPUBIND_PROCESS|HWLOC_CPUBIND_NOMEMBIND);

  int p_tidx = hwloc_bitmap_first(cpu->cpuset);
  if(init_eventset(&eventset,m->n_events,m->event_names)!=0){
    fprintf(stderr,"%d failed to init its eventset\n",tidx);
    exit(1);
  }

  pthread_cleanup_push(Monitors_thread_stop,&eventset);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
  
  struct node_box * out, * PU_vals = m->PU_vals[tidx];

  if(m->pw==NULL)
    PAPI_start(eventset);
  pthread_barrier_wait(&(m->barrier));

  for(;;){
    pthread_mutex_lock(&m->cond_mtx);
    m->uptodate=1;
    pthread_cond_wait(&(m->cond),&m->cond_mtx);
    pthread_mutex_unlock(&m->cond_mtx);
    /* update time stamp*/
    PU_vals->old_usec=PU_vals->real_usec;
    PU_vals->real_usec=PAPI_get_real_usec();
    /* gathers counters */
    if(m->pw!=NULL){
      /* A pid is specified */
      if(proc_watch_check_start_pu(m->pw,p_tidx))
	PAPI_start(eventset);
      else if(proc_watch_check_stop_pu(m->pw,p_tidx)){
	PAPI_stop(eventset,PU_vals->counters_val);
	goto next_loop;
      }
      else if(!proc_watch_get_pu_state(m->pw,p_tidx))
	goto next_loop;
    }

    PAPI_read(eventset,PU_vals->counters_val);

    /* reduce counters for every monitors */
    for(i=0;i<m->count;i++){
      obj = hwloc_get_ancestor_obj_by_depth(m->topology,m->depths[i],cpu);
      out = (struct node_box *)(obj->userdata);
      
      if(m->pw!=NULL){
	hwloc_bitmap_t b = proc_watch_get_watched_in_cpuset(m->pw,obj->cpuset);
	weight = hwloc_bitmap_weight(b);
	hwloc_bitmap_free(b);
      } else weight = hwloc_bitmap_weight(obj->cpuset);

      pthread_mutex_trylock(&(out->update_lock));
      pthread_mutex_lock(&(out->read_lock));
      /* i am the first to acquire the lock, i reset values */
      if(out->uptodate==0){
	zerod(out->counters_val,m->n_events);
	out->old_usec = out->real_usec;
	out->real_usec = 0;
      }
      /* accumulate values */
      for(j=0;j<m->n_events;j++)
	out->counters_val[j]+=PU_vals->counters_val[j];

      out->real_usec += PU_vals->real_usec;
      out->uptodate++;
      /* I am the last to update values, i update monitor value */
      if(out->uptodate==weight){
	out->uptodate=0;
	out->old_val = out->val;
	out->val = m->compute[i](out->counters_val);
	if(isinf(m->max[i]) || m->max[i]<out->val) m->max[i]= out->val;
	if(isinf(m->min[i]) || m->min[i]>out->val) m->min[i]= out->val;
	out->real_usec/=weight;
	pthread_mutex_unlock(&(out->update_lock));
      }
      pthread_mutex_unlock(&(out->read_lock));
    }
    /* signal we achieved our update */
  next_loop:;
    pthread_barrier_wait(&(m->barrier));
  }
  pthread_cleanup_pop(0);
}

int
chk_monitors_lib(const char * perf_group_filename)
{
  if(perf_group_filename==NULL)
    return 0;
  
  /* no perf group defined */
  if(access(perf_group_filename, R_OK ) == -1 )
    return 0;
  
  /* no perf group library */
  if(access(PARSED_CODE_LIB, R_OK ) == -1){
    return 0;
  }
  
  /* perf group is more recent than library */
  struct stat stat_group, stat_lib;
  if(stat(perf_group_filename,&stat_group) == -1 || stat(PARSED_CODE_LIB,&stat_lib)==-1)
    return 0;
  if(difftime(stat_group.st_mtime,stat_lib.st_mtime)>0)
    return 0;
  
  /* lib is ready for use */
  return 1;
}

void unload_monitors_lib(){
 /* get current working dir */
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL)
    return;

  char * lib = malloc(strlen(PARSED_CODE_LIB)+strlen(cwd)+2);
  lib[0]='\0';
  strcat(strcat(strcat(lib,cwd),"/"),PARSED_CODE_LIB);
  
  /* load shared libraries */
  void * dlhandle = dlopen(lib,RTLD_NOW|RTLD_GLOBAL);
  free(lib);
  if(dlhandle==NULL)
    return;
  dlclose(dlhandle);
}

void print_Monitors_header(Monitors_t m){
  unsigned int i,j;
  dprintf(m->output_fd,"#-------+--------------------+");
  for(i=0;i<m->n_events;i++){
    for(j=0;j<20;j++)
      dprintf(m->output_fd,"-");
    dprintf(m->output_fd,"+");
  }
  for(i=0;i<m->count;i++){
    for(j=0;j<22;j++)
      dprintf(m->output_fd,"-");
    dprintf(m->output_fd,"+");
  }
  dprintf(m->output_fd,"\n");

  dprintf(m->output_fd,"  PU_idx            real_usec ");
  for(i=0;i<m->n_events;i++)
    dprintf(m->output_fd,"%20s ",m->event_names[i]);
  for(i=0;i<m->count;i++){
    dprintf(m->output_fd,"%22s ",m->names[i]);
  }
  dprintf(m->output_fd,"\n");

  dprintf(m->output_fd,"#-------+--------------------+");
  for(i=0;i<m->n_events;i++){
    for(j=0;j<20;j++)
      dprintf(m->output_fd,"-");
    dprintf(m->output_fd,"+");
  }
  for(i=0;i<m->count;i++){
    for(j=0;j<22;j++)
      dprintf(m->output_fd,"-");
    dprintf(m->output_fd,"+");
  }
  dprintf(m->output_fd,"\n");
}

/***************************************************************************************************************/
/*                                                   PUBLIC                                                    */
/***************************************************************************************************************/
Monitors_t
new_default_Monitors(hwloc_topology_t topology, const char * output,unsigned int pid)
{
  char ** event_names = malloc(sizeof(char*)*2);
  event_names[0]=strdup("PAPI_FP_OPS");
  event_names[1]=strdup("PAPI_L1_DCA");
  Monitors_t m = new_Monitors(topology, 2,event_names,output,pid);
  if(m==NULL){
    fprintf(stderr,"default monitors creation failed\n");
    free(event_names[0]); free(event_names[1]); free(event_names);
    return NULL;
  }
  free(event_names[0]); free(event_names[1]); free(event_names);
  int depth = hwloc_topology_get_depth(m->topology);
  add_Monitor(m,"flops_fp",depth-1,flop_fp);
  add_Monitor(m,"L1_DCA / FP_OPS",depth-2,L1d_access_per_fpops);
  return m;
}


Monitors_t
load_Monitors(hwloc_topology_t topology, const char * perf_group_file, const char * output, unsigned int pid)
{

  if(perf_group_file==NULL){
    return NULL;
  }
  if( access( perf_group_file, R_OK ) == -1 ){
    fprintf(stderr,"perf group file:%s cannot be accessed;_n",perf_group_file);
    return NULL;
  }

  struct parsed_names * pn;
  pn = parser(perf_group_file);

  /* get current working dir */
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL){
    perror("getcwd() error");
    return NULL;
  }
  char * lib = malloc(strlen(PARSED_CODE_LIB)+strlen(cwd)+2);
  lib[0]='\0';
  strcat(strcat(strcat(lib,cwd),"/"),PARSED_CODE_LIB);

  /* load shared libraries */
  dlerror();
  void * dlhandle = dlopen(lib,RTLD_NOW);
  if(dlhandle==NULL){
    fprintf(stderr,"loading error:%s\n",dlerror());
    return NULL;
  }  
  dlerror();

  Monitors_t m = new_Monitors(topology, pn->n_events,pn->event_names,output,pid);
  free(lib);

  unsigned int i;
  for(i=0;i<pn->n_events;i++)
    free(pn->event_names[i]);
  
  for(i=0;i<pn->n_monitors;i++){
    double (*fun)(long long*) = dlsym(dlhandle,(pn->monitor_names)[i]);
    if(fun==NULL){
      fprintf(stderr,"could not load monitor function %s : %s\n",(pn->monitor_names)[i],dlerror());
      add_Monitor(m,(pn->monitor_names)[i],(pn->depths)[i],no_fun);
    }
    else
      add_Monitor(m,(pn->monitor_names)[i],(pn->depths)[i],*fun);
    free(pn->monitor_names[i]);
  }
  
  free(pn->monitor_names);
  free(pn->event_names);
  free(pn->depths);
  free(pn);
  return m;
}

void
delete_Monitors(Monitors_t m)
{
  if(m==NULL)
    return;
  unsigned int i;
  hwloc_obj_t PU;
  int n_obj;

  for(i=0;i<m->n_PU;i++)
    pthread_cancel(m->pthreads[i]);

  /** BLOCK ON JOIN even when every thread is waiting on cond_wait ! **/
  /* for(i=0;i<m->n_PU;i++){ */
  /*   pthread_join(m->pthreads[i],NULL); */
  /* } */

  pthread_cond_destroy(&(m->cond));
  pthread_mutex_destroy(&(m->cond_mtx));
  pthread_barrier_destroy(&(m->barrier));
  free(m->pthreads);

  for(i=0;i<m->count;i++){
    free(m->names[i]);
    n_obj=hwloc_get_nbobjs_by_depth(m->topology,m->depths[i]);
    while(n_obj--){
      PU=hwloc_get_obj_by_depth(m->topology,m->depths[i],n_obj);
      delete_counters_output((struct node_box *)PU->userdata);
    }
  }
  for(i=0;i<m->n_PU;i++){
    delete_counters_output(m->PU_vals[i]);
  }

  free(m->PU_vals);
  free(m->compute);
  free(m->min);
  free(m->max);
  free(m->depths);
  free(m->names);

  for(i=0;i<m->n_events;i++){
    free(m->event_names[i]);
  }
  free(m->event_names);
  delete_proc_watch(m->pw);
  free(m);
}

int
Monitors_start(Monitors_t m)
{
 print_Monitors_header(m);
 unsigned int i; int err;
 for(i=0;i<m->n_PU;i++){
   err=pthread_create(&(m->pthreads[i]),NULL,Monitors_thread,(void*)m);
   if(err!=0){
     fprintf(stderr,"pthread create failed with err: %d\n",err);
     exit(1);
   }
 }
 Monitors_wait_update(m);
 return 0;
}

void
Monitors_update_counters(Monitors_t m){
  pthread_mutex_lock(&m->cond_mtx);
  m->uptodate=0;
  pthread_cond_broadcast(&(m->cond));
  pthread_mutex_unlock(&m->cond_mtx);
  if(m->pw!=NULL)
    proc_watch_update(m->pw);
}

inline void
Monitors_wait_update(Monitors_t m){
  while(!m->uptodate) sched_yield();
}

inline long long
Monitors_get_counter_value(Monitors_t m,unsigned int counter_idx,unsigned int PU_idx)
{
  hwloc_obj_t obj =hwloc_get_obj_by_depth(m->topology,hwloc_topology_get_depth(m->topology)-1,PU_idx);
  return ((struct node_box *)(obj->userdata))->counters_val[counter_idx];
}

#define Monitors_get_monitor_max(m,i) m->max[i]
#define Monitors_get_monitor_min(m,i) m->min[i]


inline double 
Monitors_get_monitor_value(Monitors_t m, unsigned int m_idx, unsigned int sibling_idx)
{ 
  hwloc_obj_t obj = hwloc_get_obj_by_depth(m->topology,m->depths[m_idx],sibling_idx);
  return ((struct node_box *)(obj->userdata))->val;
}

inline double 
Monitors_get_monitor_variation(Monitors_t m, unsigned int m_idx, unsigned int sibling_idx)
{ 
  hwloc_obj_t obj = hwloc_get_obj_by_depth(m->topology,m->depths[m_idx],sibling_idx);
  return ((struct node_box *)(obj->userdata))->val-((struct node_box *)(obj->userdata))->old_val;
}


inline double
Monitors_wait_monitor_value(Monitors_t m, unsigned int m_idx, unsigned int sibling_idx)
{
  hwloc_obj_t obj = hwloc_get_obj_by_depth(m->topology,m->depths[m_idx],sibling_idx);
  pthread_mutex_lock(&(((struct node_box *)(obj->userdata))->update_lock));
  double val = ((struct node_box *)(obj->userdata))->val;
  pthread_mutex_unlock(&(((struct node_box *)(obj->userdata))->update_lock));
  return val;
}

inline double
Monitors_wait_monitor_variation(Monitors_t m, unsigned int m_idx, unsigned int sibling_idx)
{
  hwloc_obj_t obj = hwloc_get_obj_by_depth(m->topology,m->depths[m_idx],sibling_idx);
  pthread_mutex_lock(&(((struct node_box *)(obj->userdata))->update_lock));
  double val = ((struct node_box *)(obj->userdata))->val-((struct node_box *)(obj->userdata))->old_val;
  pthread_mutex_unlock(&(((struct node_box *)(obj->userdata))->update_lock));
  return val;
}


void 
Monitors_print(Monitors_t m)
{
  double val;
  unsigned int p_idx,l_idx,i,nobj;
  hwloc_bitmap_t cpuset = (hwloc_bitmap_t)hwloc_topology_get_topology_cpuset(m->topology);

  if(m->pw!=NULL)
    cpuset = proc_watch_get_watched_in_cpuset(m->pw, cpuset);

  hwloc_bitmap_foreach_begin(p_idx,cpuset){
    l_idx=physical_to_logical(m->topology,p_idx);
    dprintf(m->output_fd,"%8d ",l_idx);
    dprintf(m->output_fd,"%20lld ",m->PU_vals[l_idx]->real_usec);
    for(i=0;i<m->n_events;i++){
      dprintf(m->output_fd,"%20lld "  ,m->PU_vals[l_idx]->counters_val[i]);
    }
    for(i=0;i<m->count;i++){
      nobj = hwloc_get_nbobjs_by_depth(m->topology,m->depths[i]);
      val = Monitors_get_monitor_value(m,i,(l_idx*nobj/m->n_PU)%nobj);
      dprintf(m->output_fd,"%22lf",val); 
    }
    dprintf(m->output_fd,"\n");
  }
  hwloc_bitmap_foreach_end();
  
  if(m->pw!=NULL)
    hwloc_bitmap_free(cpuset);
}


void Monitors_insert_print_in_topology(Monitors_t m){
  char string_counter[2*24*m->count];
  int monitor_idx, nobj;
  hwloc_obj_t obj;
  for(monitor_idx=0;monitor_idx<m->count;monitor_idx++){
    nobj = hwloc_get_nbobjs_by_depth(m->topology,m->depths[monitor_idx]);
    while(nobj--){
      obj = hwloc_get_obj_by_depth(m->topology, m->depths[monitor_idx], nobj);
      double val = Monitors_get_monitor_value(m,monitor_idx,nobj);
      sprintf(string_counter,"%ss: %22lf",m->names[monitor_idx],val);
      if(!hwloc_topology_insert_misc_object_by_parent(m->topology, obj, string_counter)){
	fprintf(stderr, "Failed to insert counter %s\n", m->names[monitor_idx]);
      }
    }
  }      
}

void Monitors_print_in_topology(Monitors_t m){
  int monitor_idx, nobj;
  hwloc_obj_t obj;
  for(monitor_idx=0;monitor_idx<m->count;monitor_idx++){
    nobj = hwloc_get_nbobjs_by_depth(m->topology,m->depths[monitor_idx]);
    while(nobj--){
      obj = hwloc_get_obj_by_depth(m->topology, m->depths[monitor_idx], nobj)->children[1];
      double val = Monitors_get_monitor_value(m,monitor_idx,nobj);
      sprintf(&(obj->name[0]),"%ss: %22lf",m->names[monitor_idx],val);
    }
  }      
}


