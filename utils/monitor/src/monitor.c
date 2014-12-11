#include <errno.h>
#include <hwloc.h>
#include <papi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>
#include <dirent.h>
#include "parser.h"
#include "monitor.h"


struct monitors{
  hwloc_topology_t  topology;
  unsigned int      pid;
  DIR           *   p_dir;

  int               output_fd; 
  fd_set            output_fd_set; 

  int               n_events;    
  char        **    event_names;   /* n_events * strlen(event_name) */
  long long         real_usec, old_usec;

  unsigned int      n_PU;
  pthread_t       * pthreads;                        /* nb_PU */
  unsigned int    * monitoring_core;                 /* nb_PU */
  pthread_cond_t    cond;
  pthread_mutex_t   cond_mtx;
  pthread_mutex_t   update_mtx;
  unsigned int      uptodate;

  unsigned int      count; 
  unsigned int      allocated_count; 
  char        **    names;             /* nb_monitors */
  double  (**compute)(long long *);    /* nb_monitors */
  unsigned int *    depths;            /* nb_monitors */
 };

struct counters_output{
  long long * counters_val;
  double val;
  double old_val;
  int uptodate;
  pthread_mutex_t read_lock;
  pthread_mutex_t update_lock;
};

struct counters_output *
new_counters_output(unsigned int n_events)
{
  struct counters_output * out = malloc(sizeof(struct counters_output));
  if(out==NULL){
    perror("malloc failed\n");
    return NULL;
  }
  if((out->counters_val=calloc(n_events,sizeof(double)))==NULL){
    perror("calloc failed\n");
    free(out);
    return NULL;
  }
  pthread_mutex_init(&(out->read_lock),NULL);
  pthread_mutex_init(&(out->update_lock),NULL);
  return out;
}

void
delete_counters_output(struct counters_output * out)
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
    fprintf(stderr,"\n");
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

Monitors_t new_Monitors(unsigned int n_events, char ** event_names, const char * output, unsigned int pid){
  if(event_names==NULL){
    return NULL;
  }

  /* try to initialize an individual eventset to check counters availability */
  int eventset;
  if(init_eventset(&eventset,n_events,event_names)!=0){
    return NULL;
  }
  PAPI_destroy_eventset(&eventset);

  unsigned int i; int err;
  Monitors_t m = malloc(sizeof(struct monitors));
  if(m==NULL){
    perror("malloc failed\n");
    return NULL;
  }
  m->n_events=0;
  m->pid=pid;
  m->event_names=NULL;
  m->real_usec=0;
  m->old_usec=0;
  m->n_PU=0;
  m->pthreads=NULL;
  m->count=0;
  m->allocated_count=0;
  m->uptodate=0;
  m->names=NULL;
  m->depths=NULL;
  FD_ZERO(&(m->output_fd_set));
  m->output_fd=0;
  m->p_dir=NULL;
  /* set condition for reading counters */
  pthread_mutex_init(&(m->cond_mtx),NULL);
  pthread_mutex_init(&(m->update_mtx),NULL);
  if((err=pthread_cond_init(&(m->cond),NULL))!=0){
    fprintf(stderr,"failed to initialize mutex\n");
    return NULL;
  }
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
  FD_SET(m->output_fd,&(m->output_fd_set));

  /* Initialize topology */
  unsigned long flags = HWLOC_TOPOLOGY_FLAG_IO_DEVICES | HWLOC_TOPOLOGY_FLAG_ICACHES;
  hwloc_topology_init(&(m->topology));
  hwloc_topology_set_flags(m->topology, flags);
  hwloc_topology_load(m->topology);

  /* count core number */
  int depth = hwloc_topology_get_depth(m->topology);
  m->n_PU = hwloc_get_nbobjs_by_depth(m->topology, depth-1);  
  hwloc_obj_t PU;
  if((m->pthreads=malloc(sizeof(pthread_t)*m->n_PU))==NULL){
    delete_Monitors(m); return NULL;
  }
  if((m->monitoring_core=malloc(sizeof(int)*m->n_PU))==NULL){
    delete_Monitors(m); return NULL;
  }
  for(i=0;i<m->n_PU;i++){
    m->pthreads[i]=0;
    m->monitoring_core[i]=0;
    PU=hwloc_get_obj_by_depth(m->topology,depth-1,i);
    if(PU==NULL){
      fprintf(stderr,"hwloc returned a NULL object for PU[%d] at depth %d\n",i,depth-1);
      delete_Monitors(m); return NULL;
    }
    PU->userdata=new_counters_output(n_events);
    if(PU->userdata==NULL){
      delete_Monitors(m); return NULL;
    }
  }

  /* allocate default space for monitors depths,formulas*/
  m->count=0;
  m->allocated_count=4;
  if((m->depths=malloc(sizeof(unsigned int)*m->allocated_count))==NULL){
    delete_Monitors(m); return NULL;
  }
  for(i=0;i<m->allocated_count;i++)
    m->depths[i]=0;
  if((m->compute = malloc(sizeof(double (*)(long long*))*m->allocated_count))==NULL){
    delete_Monitors(m); return NULL;
  }
  if((m->names = malloc(sizeof(char*)*m->allocated_count))==NULL){
    delete_Monitors(m); return NULL;
  }

  /* allocate event names and eventset*/
  m->n_events = n_events;
  if((m->event_names = malloc(sizeof(event_names)))==NULL){
    delete_Monitors(m); return NULL;
  }
  for(i=0;i<m->n_events;i++)
    m->event_names[i]=strdup(event_names[i]);

  /* allocate space for event values */

  return m;
}

int add_Monitor(Monitors_t m, const char * name, unsigned int depth, double (*fun)(long long *)){
  if(m==NULL)
    return 1;
  int n_obj=hwloc_get_nbobjs_by_depth(m->topology,depth);
  if(n_obj==0){
    fprintf(stderr,"No object in topology at depth %d\n",depth);
    return 1;
  }
  if(m->allocated_count<=m->count){
    m->allocated_count*=2;
    if((m->names=realloc(m->names, sizeof(char*)*m->allocated_count))==NULL)
      return 1;
    if((m->depths=realloc(m->depths, sizeof(int)*m->allocated_count))==NULL)
      return 1;
    if((m->compute=realloc(m->compute, sizeof(double(*)(long long*))*m->allocated_count))==NULL)
      return 1;
  }
  if(bsearch(&depth,m->depths,m->count,sizeof(unsigned int),compareint)){
    fprintf(stderr,"cannot insert monitor %s at depth %d because another monitor already exists at this depth\n",
	    name,depth);
    return 1;
  }
  hwloc_obj_t PU;
  while(n_obj--){
    PU=hwloc_get_obj_by_depth(m->topology,depth,n_obj);
    PU->userdata = new_counters_output(m->n_events);
    if(PU->userdata==NULL)
      return 1;
  }
  
  m->names[m->count] = strdup(name);
  m->depths[m->count] = depth;
  m->compute[m->count] = fun;
  m->count++;
  return 0;
}

void*
Monitors_thread(void* monitors){
  if(monitors==NULL)
    pthread_exit(NULL);
  Monitors_t m = (Monitors_t)monitors;
  pthread_t tid = pthread_self();
  unsigned int i,j;
  int tidx=-1;
  for(i=0;i<m->n_PU;i++){
    if(m->pthreads[i]==tid){
      tidx=i;
      break;
    }
  }
  int eventset = PAPI_NULL;
  if(init_eventset(&eventset,m->n_events,m->event_names)!=0){
    fprintf(stderr,"%d failed to init its eventset\n",tidx);
    exit(1);
  }
  /* bind my self to tidx core */
  int depth=hwloc_topology_get_depth(m->topology);
  hwloc_obj_t obj,cpu = hwloc_get_obj_by_depth(m->topology,depth-1,tidx);
  if(cpu==NULL || cpu->cpuset==NULL){
    fprintf(stderr,"obj[%d] at depth %d is null\n",tidx,depth-1);
    exit(1);
  }
  hwloc_set_cpubind(m->topology, cpu->cpuset, HWLOC_CPUBIND_STRICT|HWLOC_CPUBIND_PROCESS|HWLOC_CPUBIND_NOMEMBIND);

  int err;
  if((err=PAPI_start(eventset))!=PAPI_OK){
    fprintf(stderr,"eventset init: cannot start counting events\n");
    handle_error(err);
    return NULL;
  }
  
  struct counters_output * out, * PU_vals = (struct counters_output *) cpu->userdata;
  
  while(1){
    /* A pid is specified */
    if(m->pid!=0)
      {
	/* the pid isn't currently running or no pid child is running on the same PU as me */
	if(!m->monitoring_core[tidx])
	      goto next_loop;
      }
    /* gathers counters */
    PAPI_read(eventset,PU_vals->counters_val);
    /* reduce counters for every monitors */
    for(i=0;i<m->count;i++){
      if(m->depths[i]==(depth-1)){
	out = (struct counters_output *)(cpu->userdata);
    	out->old_val = out->val;
    	out->val=m->compute[i](out->counters_val);
    	continue;
      }
      obj = hwloc_get_ancestor_obj_by_depth(m->topology,m->depths[i],cpu);
      out = (struct counters_output *)(obj->userdata);
      pthread_mutex_trylock(&(out->update_lock));
      pthread_mutex_lock(&(out->read_lock));
      /* i am the first to acquire the lock, i reset values */
      if(out->uptodate==hwloc_bitmap_weight(obj->cpuset)){
    	out->uptodate=0;
    	memset(out->counters_val,0.0,sizeof(double)*m->n_events);
      }
      /* accumulate values */
      for(j=0;j<m->n_events;j++)
    	out->counters_val[j]+=PU_vals->counters_val[j];
      out->uptodate++;
      /* I am the last to update values, i update monitor value */
      if(out->uptodate==hwloc_bitmap_weight(obj->cpuset)){
    	out->old_val = out->val;
    	out->val=m->compute[i](out->counters_val);
	/* I update time counter */
	m->old_usec=m->real_usec;
	m->real_usec=PAPI_get_real_usec();
    	pthread_mutex_unlock(&(out->update_lock));
      }
      pthread_mutex_unlock(&(out->read_lock));
    }

      next_loop:;
    /* signal we achieved our update */
    pthread_mutex_lock(&(m->update_mtx));
    m->uptodate++;
    pthread_mutex_unlock(&(m->update_mtx));
    pthread_cond_wait(&(m->cond),&(m->cond_mtx));
  }

  PAPI_stop(eventset,PU_vals->counters_val);
  PAPI_destroy_eventset(&eventset);
  return NULL;
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
  char name[21];
  for(i=0;i<m->count;i++){
    strncpy(name,m->names[i],21);
    dprintf(m->output_fd,"%22s ",name);
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
int
Monitors_init()
{
  /* initialize PAPI lib */
  int retval;
  retval = PAPI_library_init(PAPI_VER_CURRENT);
  if (retval != PAPI_VER_CURRENT && retval > 0) {
    fprintf(stderr,"PAPI library version mismatch!\en");
    return 1; 
  }
  if (retval < 0)
    handle_error(retval);
  retval = PAPI_is_initialized();
  if (retval != PAPI_LOW_LEVEL_INITED)
    handle_error(retval) ;
  PAPI_multiplex_init();
  if(PAPI_thread_init(pthread_self)!=PAPI_OK){
    perror("could not initialize PAPI_threads\n");
    return 1;
  }
  return 0;
}  

Monitors_t
new_default_Monitors(const char * output,unsigned int pid)
{
  char ** event_names = malloc(sizeof(char*)*2);
  event_names[0]=strdup("PAPI_FP_OPS");
  event_names[1]=strdup("PAPI_L1_DCA");
  Monitors_t m = new_Monitors(2,event_names,output,pid);
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
load_Monitors(const char * perf_group_file, const char * output, unsigned int pid)
{

  if(perf_group_file==NULL){
    return NULL;
  }
  if( access( perf_group_file, R_OK ) == -1 ){
    fprintf(stderr,"perf group file:%s cannot be accessed;_n",perf_group_file);
    return NULL;
  }
  /* create the dynamic library if it does not exists */
  if(!chk_monitors_lib(perf_group_file)){
    parser(perf_group_file);
  }

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
  void * dlhandle = dlopen(lib,RTLD_NOW | RTLD_GLOBAL);
  free(lib);
  if(dlhandle==NULL){
    fprintf(stderr,"loading error:%s\n",dlerror());
    return NULL;
  }  
  char** event_names    = *(char***)dlsym(dlhandle,"event_names"   );
  if(event_names==NULL){
    fprintf(stderr,"could not load event_names: %s\n",dlerror());
    return NULL;
  }
  unsigned int * n_events = (unsigned int *)dlsym(dlhandle,"n_events");
  if(n_events==NULL){
    fprintf(stderr,"could not load n_events: %s\n",dlerror());
    return NULL;
  }

  unsigned int * n_monitors = (unsigned int *)dlsym(dlhandle,"n_monitors");
  if(n_monitors==NULL){
    fprintf(stderr,"could not load n_monitors: %s\n",dlerror());
    return NULL;
  }

  Monitors_t m = new_Monitors(*n_events,event_names,output,pid);

  char** monitor_names  = *(char***)dlsym(dlhandle,"monitor_names" );
  if(monitor_names==NULL){
    fprintf(stderr,"could not load monitor_names: %s\n",dlerror());
    return NULL;
  }

  int *  monitor_depths = *(int**)  dlsym(dlhandle,"monitor_depths");
  if(monitor_depths==NULL){
    fprintf(stderr,"could not load monitor_depths: %s\n",dlerror());
    return NULL;
  }

  unsigned int i;
  for(i=0;i<*n_monitors;i++){
    double (*fun)(long long*) = (double (*)(long long*))dlsym(dlhandle,monitor_names[i]);
    if(fun==NULL){
      fprintf(stderr,"could not load monitor function %s : %s\n",monitor_names[i],dlerror());
      add_Monitor(m,monitor_names[i],monitor_depths[i],no_fun);
    }
    else
      add_Monitor(m,monitor_names[i],monitor_depths[i],fun);
  }
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

  for(i=0;i<m->n_PU;i++){
    pthread_cancel(m->pthreads[i]);
  }
  pthread_cond_destroy(&(m->cond));
  pthread_mutex_destroy(&(m->cond_mtx));
  pthread_mutex_destroy(&(m->update_mtx));
  free(m->pthreads);
  free(m->monitoring_core);
  if(m->p_dir!=NULL)
    closedir(m->p_dir);
  for(i=0;i<m->count;i++){
    free(m->names[i]);
    n_obj=hwloc_get_nbobjs_by_depth(m->topology,m->depths[i]);
    while(n_obj--){
      PU=hwloc_get_obj_by_depth(m->topology,m->depths[i],n_obj);
      delete_counters_output((struct counters_output *)PU->userdata);
    }
  }

  free(m->compute);
  free(m->depths);
  free(m->names);

  for(i=0;i<m->n_events;i++){
    free(m->event_names[i]);
  }
  free(m->event_names);
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
     return 1;
   }
 }
 return 0;
}


void
Monitors_update_counters(Monitors_t m){
  /* update threads which have to monitor */
  if(m->pid!=0 && kill(m->pid,0)!=-1){
    /* open proc dir it is not already openned*/
    if(m->p_dir==NULL){
      char proc_dir_path[11+strlen("/proc//task")];
      sprintf(proc_dir_path,"/proc/%d/task",m->pid);
      m->p_dir = opendir(proc_dir_path);
      if(m->p_dir==NULL){
	fprintf(stderr,"cannot open %s\n",proc_dir_path);
	delete_Monitors(m);
	exit(1);
      }
    }
    /* update active cores list */
    struct dirent * task_dir;
    char c;
    char pu_num[11];
    int  pu_n;
    unsigned int monitoring_core[m->n_PU],i;
    FILE * task;
    for(i=0;i<m->n_PU;i++)
      monitoring_core[i]=0;
    /* look into each pid's task's stat file */
    while((task_dir=readdir(m->p_dir))!=NULL){
      if(!strcmp(task_dir->d_name,".") || !strcmp(task_dir->d_name,".."))
	continue;
      memset(pu_num,0,11);
      char file_name[11+strlen("/proc//task//stat")+strlen(task_dir->d_name)];
      sprintf(file_name,"/proc/%d/task/%s/stat",m->pid,task_dir->d_name);
      //fprintf(stderr,"looking in %s for thread PU\n",file_name);
      task = fopen(file_name,"r");
      if(task == NULL)
	continue;
      /* move cursor to PU id */
      for(i=0;i<38;i++){
	while((c=fgetc(task))!=' ');
      }
      /* read PU id */
      while((c=fgetc(task))!=' ')
	strcat(pu_num,&c);
      fclose(task);
      //fprintf(stderr,"monitoring core %d\n",atoi(pu_num));
      pu_n=atoi(pu_num);
      monitoring_core[pu_n]=1;
    }
    rewinddir(m->p_dir);
    memcpy(m->monitoring_core,monitoring_core,sizeof(monitoring_core));
    /* fprintf(stderr,"monitoring_PU: "); */
    /* for(i=0;i<m->n_PU;i++) */
    /*   fprintf(stderr,"%d ",monitoring_core[i]); */
    /* fprintf(stderr,"\n"); */
  }

  pthread_mutex_lock(&(m->update_mtx));
  if(m->uptodate==m->n_PU){
    m->uptodate=0;
    pthread_cond_broadcast(&(m->cond));
  }
  pthread_mutex_unlock(&(m->update_mtx));
}

void
Monitors_wait_update(Monitors_t m){
 start:
  pthread_mutex_lock(&(m->update_mtx));
  if(m->uptodate!=m->n_PU){
    pthread_mutex_unlock(&(m->update_mtx));
    sched_yield();
    goto start;
  }
  pthread_mutex_unlock(&(m->update_mtx));
}

long long
Monitors_get_counter_value(Monitors_t m,unsigned int counter_idx,unsigned int PU_idx)
{
  hwloc_obj_t obj =hwloc_get_obj_by_depth(m->topology,hwloc_topology_get_depth(m->topology)-1,PU_idx);
  return ((struct counters_output *)(obj->userdata))->counters_val[counter_idx];
}

double 
Monitors_get_monitor_value(Monitors_t m, unsigned int m_idx, unsigned int sibling_idx)
{ 
  hwloc_obj_t obj = hwloc_get_obj_by_depth(m->topology,m->depths[m_idx],sibling_idx);
  return ((struct counters_output *)(obj->userdata))->val;
}

double
Monitors_wait_monitor_value(Monitors_t m, unsigned int m_idx, unsigned int sibling_idx)
{
  hwloc_obj_t obj = hwloc_get_obj_by_depth(m->topology,m->depths[m_idx],sibling_idx);
  pthread_mutex_lock(&(((struct counters_output *)(obj->userdata))->update_lock));
  double ret = ((struct counters_output *)(obj->userdata))->val;
  pthread_mutex_unlock(&(((struct counters_output *)(obj->userdata))->update_lock));
  return ret;
}

void 
Monitors_print(Monitors_t m)
{
  char string[9+21+(21*m->count)+2+(21*m->n_events)];
  char * str = string;
  unsigned int PU_idx,event_idx,monitor_idx,nobj;
  for(PU_idx = 0; PU_idx<m->n_PU;PU_idx++){
    str=string;
    str+=sprintf(str,"%8d ",PU_idx);
    str+=sprintf(str,"%20lld ",m->real_usec);
    for(event_idx=0;event_idx<m->n_events;event_idx++){
      str+=sprintf(str,"%20lld "  ,Monitors_get_counter_value(m,event_idx,PU_idx));
    }
    for(monitor_idx=0;monitor_idx<m->count;monitor_idx++){
      nobj = hwloc_get_nbobjs_by_depth(m->topology,m->depths[monitor_idx]);
      double val = Monitors_get_monitor_value(m,monitor_idx,(PU_idx*nobj/m->n_PU)%nobj);
      str+=sprintf(str,"%22f "  ,val);
    }
    str+=sprintf(str,"\n");
    write(m->output_fd, string, strlen(string));
  }
}


