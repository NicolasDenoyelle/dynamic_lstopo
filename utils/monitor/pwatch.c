#include "pwatch.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <search.h>
#include "hwloc/linux.h"

struct proc_watch{
  hwloc_topology_t   topology;
  unsigned int       pid;
  char             * p_dir_path;
  DIR              * p_dir;
  unsigned           n_PU, n_tasks, alloc_tasks;
  unsigned int    ** tasks;    /* [n_tasks][tid, PU_num, state] */
  hwloc_bitmap_t     state;       /* physical */ /* 0 = stopped,   1 = started */
  hwloc_bitmap_t     query_state; /* physical */ /* 0 = must stop, 1 = must start */
  pthread_mutex_t    lock;        
};

#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + 16))

int  compare_tasks(const void* a,const void* b ){return ((unsigned int *)a)[0] - ((unsigned int *)b)[0];}

struct task_stat{
  unsigned pu_num;
  unsigned state; // 1=RUNNING, 0=NOT RUNNING
};

void
read_task(const char * task_path, struct task_stat * t_info)
{
  FILE * task = fopen(task_path, "r");
  if(task == NULL){
    t_info->state=0;
    return;
  }

  char state;  
  fscanf(task,"%*d %*s %c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %*u %*u %*d %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*d %d",&state,&(t_info->pu_num));
  fclose(task);

  if(state!='R')
    t_info->state=0;
  else
    t_info->state=1;
  return;
}

inline int
physical_to_logical(hwloc_topology_t topology, int physical_idx){
  return hwloc_get_pu_obj_by_os_index(topology,physical_idx)->logical_index;
}

inline int
logical_to_physical(hwloc_topology_t topology, int logical_idx){
  return hwloc_bitmap_first(hwloc_get_obj_by_depth(topology,hwloc_topology_get_depth(topology)-1,logical_idx)->cpuset);
}

int
proc_watch_check_start_pu(struct proc_watch * pw,unsigned int physical_PU)
{
  if(!hwloc_bitmap_isset(pw->state,physical_PU) && hwloc_bitmap_isset(pw->query_state,physical_PU)){
    hwloc_bitmap_set(pw->state,physical_PU);
    return 1;
  }
  return 0;
}

inline unsigned int proc_watch_get_pid(struct proc_watch * pw){
  return pw->pid;
}

int
proc_watch_check_stop_pu(struct proc_watch * pw, unsigned int physical_PU)
{
  if(hwloc_bitmap_isset(pw->state,physical_PU) && !hwloc_bitmap_isset(pw->query_state,physical_PU)){
    hwloc_bitmap_clr(pw->state,physical_PU);
    return 1;
  }
  return 0;
}

inline int 
proc_watch_get_pu_state(struct proc_watch * pw, unsigned int physical_PU)
{
  return hwloc_bitmap_isset(pw->state,physical_PU);
}

inline void
proc_watch_get_watched_in_cpuset(struct proc_watch * pw, hwloc_cpuset_t cpuset, hwloc_cpuset_t out)
{
  if(pw!=NULL)
    hwloc_bitmap_and(out,cpuset,pw->state);
}


void 
proc_watch_update(struct proc_watch * pw)
{
  struct dirent * task_dir;
  char task_path[128];
  hwloc_bitmap_t query_state;

  /* If the process is not running, stop monitoring */
  if(kill(pw->pid,0)!=0){
    hwloc_bitmap_zero(pw->query_state);
    return;
  }

  /* If the process starts, we store its directory */
  if(pw->p_dir==NULL){
    pw->p_dir = opendir(pw->p_dir_path);
    if(pw->p_dir==NULL){
      return;
    }
  }

  query_state = hwloc_bitmap_alloc();
  pw->n_tasks=0;

  struct task_stat * t_info = malloc(sizeof(struct task_stat));
  if(t_info==NULL){
    perror("malloc");
    exit(1);
  }

  /* look into each pid thread to stat file */
  while((task_dir=readdir(pw->p_dir))!=NULL){
    if(!strcmp(task_dir->d_name,".") || !strcmp(task_dir->d_name,".."))
      continue;

    sprintf(task_path,"/proc/%d/task/%s/stat",pw->pid,task_dir->d_name);
    read_task(task_path,t_info);
    if(t_info->state)
      hwloc_bitmap_set(query_state,t_info->pu_num);
  }

  free(t_info);
  hwloc_bitmap_copy(pw->query_state,query_state);
  hwloc_bitmap_free(query_state);
  rewinddir(pw->p_dir);
}


struct proc_watch *
new_proc_watch(hwloc_topology_t topo, unsigned int pid, unsigned int n_tasks)
{
  int i;
  char tmp [11+strlen("/proc//task")];
  struct proc_watch * pw;
  pw = malloc(sizeof (struct proc_watch));
  if(pw==NULL){
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  pw->topology = topo;
  pw->n_PU=hwloc_get_nbobjs_by_depth(topo,hwloc_topology_get_depth(topo)-1);
  pw->pid=pid;
  sprintf(tmp,"/proc/%d/task",pid);
  pw->p_dir_path = strdup(tmp);
  pw->p_dir=NULL;
  pw->tasks = NULL;
  pw->n_tasks=0;
  pw->alloc_tasks=2*pw->n_PU;

  if((pw->tasks = malloc(sizeof(unsigned int *)*pw->alloc_tasks))==NULL){
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  
  for(i=0;i<pw->alloc_tasks;i++){
    if((pw->tasks[i] = malloc(sizeof(unsigned int)*3))==NULL){
      perror("malloc");
      exit(EXIT_FAILURE);
    }
  }
  
  if((pw->state = hwloc_bitmap_alloc())==NULL){
    perror("hwloc_bitmap_alloc()");
    exit(EXIT_FAILURE);
  }
  if((pw->query_state = hwloc_bitmap_alloc())==NULL){
    perror("hwloc_bitmap_alloc()");
    exit(EXIT_FAILURE);
  }

  pthread_mutex_init(&(pw->lock),NULL);
  proc_watch_update(pw);
  return pw;
}

void
delete_proc_watch(struct proc_watch * pw){
  unsigned int i;
  if(pw==NULL)
    return;
  pthread_mutex_destroy(&(pw->lock));
  for(i=0;i<pw->alloc_tasks;i++){
    free(pw->tasks[i]);
  }
  free(pw->tasks);
  hwloc_bitmap_free(pw->state);
  hwloc_bitmap_free(pw->query_state);
  free(pw->p_dir_path);
  closedir(pw->p_dir);
  free(pw);
}

