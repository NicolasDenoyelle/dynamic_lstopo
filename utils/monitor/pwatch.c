#include "pwatch.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "hwloc/linux.h"

struct proc_watch{
  hwloc_topology_t   topology;
  unsigned int       pid;
  char             * p_dir_path;
  DIR              * p_dir;
  int                n_PU;
  unsigned int    ** tasks;       /* physical n_PU*tasks[n_PU][0] */
  hwloc_bitmap_t     state;       /* physical */ /* 0 = stopped,   1 = started */
  hwloc_bitmap_t     query_state; /* physical */ /* 0 = must stop, 1 = must start */
  pthread_mutex_t  * lock;        /* physical n_PU */
};

#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + 16))

int  compareuint(const void* a,const void* b ){return (int)(*(unsigned int *)a - *(unsigned int *)b);}

int
read_task_core(const char * task_path)
{
  FILE * task = fopen(task_path, "r");
  int pu_n=-1;
  if(task == NULL)
    return -1;
  
  fscanf(task,"%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %*u %*u %*d %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*d %d",&pu_n);
  fclose(task);
  return pu_n;
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
proc_watch_add_task(struct proc_watch * pw, unsigned int task)
{
  char file_name[128];
  int pu;

  sprintf(file_name,"/proc/%d/task/%d/stat",pw->pid,task);
  pu = read_task_core(file_name);
  if(pu==-1)
    return;
  pthread_mutex_lock(&(pw->lock[pu]));
  pw->tasks[pu][pw->tasks[pu][0]]=task;
  hwloc_bitmap_set(pw->query_state,pu);
  if((++pw->tasks[pu][0])>pw->tasks[pu][1]){
    pw->tasks[pu][1]*=2;
    pw->tasks[pu] = realloc(pw->tasks[pu],sizeof(int)*pw->tasks[pu][1]);
  }
  qsort(&(pw->tasks[pu][2]),pw->tasks[pu][0],sizeof(int),compareuint);
  pthread_mutex_unlock(&(pw->lock[pu]));
}

void
proc_watch_add_task_on_pu(struct proc_watch * pw, unsigned int task, unsigned int physical_PU)
{
  pthread_mutex_lock(&(pw->lock[physical_PU]));
  pw->tasks[physical_PU][pw->tasks[physical_PU][0]]=task;
  hwloc_bitmap_set(pw->query_state,physical_PU);
  if((++pw->tasks[physical_PU][0])>pw->tasks[physical_PU][1]){
    pw->tasks[physical_PU][1]*=2;
    pw->tasks[physical_PU] = realloc(pw->tasks[physical_PU],sizeof(int)*pw->tasks[physical_PU][1]);
  }
  qsort(&(pw->tasks[physical_PU][2]),pw->tasks[physical_PU][0],sizeof(int),compareuint);
  pthread_mutex_unlock(&(pw->lock[physical_PU]));
}

int
proc_watch_rm_task_on_pu(struct proc_watch * pw, unsigned int task, unsigned int physical_PU)
{
  unsigned int tidx, *t;

  if(pw->tasks[physical_PU][0]<=2)
    return -1;
  if(pw->tasks[physical_PU][0]==3){
    if(pw->tasks[physical_PU][2] == task){
      pthread_mutex_lock(&(pw->lock[physical_PU]));
      pw->tasks[physical_PU][0]=2;
      hwloc_bitmap_clr(pw->query_state,physical_PU);
      pthread_mutex_unlock(&(pw->lock[physical_PU]));
      return 2;
    }
    else {
      return -1;
    }
  }
  
  t = bsearch(&task,&(pw->tasks[physical_PU][2]),pw->tasks[physical_PU][0],sizeof(int),compareuint);
  if(t==NULL)
    return -1;
  tidx = (t-pw->tasks[physical_PU])/sizeof(int);
  pthread_mutex_lock(&(pw->lock[physical_PU]));
  memcpy(&(pw->tasks[physical_PU][tidx]),&(pw->tasks[physical_PU][tidx+1]),pw->tasks[physical_PU][0]-tidx-1);
  pw->tasks[physical_PU][0]--;
  pthread_mutex_unlock(&(pw->lock[physical_PU]));
  return tidx;
}

int
proc_watch_rm_task(struct proc_watch * pw, unsigned int task)
{
  unsigned int i, ret;
  for(i=0;i<pw->n_PU;i++){
    if( (ret = proc_watch_rm_task_on_pu(pw, task, i)) != -1)
      return ret;
  }
  return -1;
}

void 
proc_watch_update(struct proc_watch * pw)
{
  struct dirent * task_dir;
  unsigned int  **old_tasks, ** tasks, i, pu;
  hwloc_bitmap_t query_state;
  char * file_name;
  if(kill(pw->pid,0)!=0){
    hwloc_bitmap_zero(pw->query_state);
    return;
  }
  if(pw->p_dir==NULL){
    pw->p_dir = opendir(pw->p_dir_path);
    if(pw->p_dir==NULL){
      return;
    }
  }

  old_tasks = pw->tasks;

  query_state = hwloc_bitmap_alloc();
  tasks = calloc(pw->n_PU,sizeof(int*));
  for(i=0;i<pw->n_PU;i++){
    tasks[i] = calloc(pw->n_PU,sizeof(int));
    tasks[i][1]=pw->n_PU;
    tasks[i][0]=2;
  }

  /* look into each pid thread to stat file */
  while((task_dir=readdir(pw->p_dir))!=NULL){
      if(!strcmp(task_dir->d_name,".") || !strcmp(task_dir->d_name,".."))
	continue;
      file_name = malloc(11+strlen("/proc//task//stat")+strlen(task_dir->d_name));
      sprintf(file_name,"/proc/%d/task/%s/stat",pw->pid,task_dir->d_name);
      pu=read_task_core(file_name);
      free(file_name);
      tasks[pu][tasks[pu][0]]=atoi(task_dir->d_name);
      if((++tasks[pu][0])>tasks[pu][1]){
	tasks[pu][1]*=2;
	tasks[pu] = realloc(tasks[pu],sizeof(int)*tasks[pu][1]);
      }
    hwloc_bitmap_set(query_state,pu);
  }

  for(i=0;i<pw->n_PU;i++)
    qsort(&(tasks[i][2]),tasks[i][0],sizeof(int),compareuint);
  
  pw->tasks=tasks;
  hwloc_bitmap_copy(pw->query_state,query_state);

  if(old_tasks!=NULL){
    for(i=0;i<pw->n_PU;i++)
      free(old_tasks[i]);
    free(old_tasks);
  }
  hwloc_bitmap_free(query_state);
  rewinddir(pw->p_dir);
}


void 
proc_watch_update_pu(struct proc_watch * pw, int physical_pu)
{
  unsigned int i,pu;
  hwloc_cpuset_t cpuset; 
  unsigned int ** tasks;

  cpuset=hwloc_bitmap_alloc();
  tasks= pw->tasks;
  for(i=2;i<pw->tasks[physical_pu][0];i++){
    hwloc_linux_get_tid_cpubind(pw->topology,tasks[physical_pu][i],cpuset);
    pu=hwloc_bitmap_first(cpuset);
    if(pu!=physical_pu){
      proc_watch_add_task_on_pu(pw, tasks[physical_pu][i], pu);
      pthread_mutex_lock(&(pw->lock[physical_pu]));
      memcpy(&(tasks[physical_pu][i]),&(tasks[physical_pu][i+1]),tasks[physical_pu][0]-i-1);
      if(--tasks[physical_pu][0]==2)
	hwloc_bitmap_clr(pw->query_state,physical_pu);
      pthread_mutex_unlock(&(pw->lock[physical_pu]));
    }
  }
  hwloc_bitmap_free(cpuset);
}

void 
proc_watch_update_tasks(struct proc_watch * pw)
{
  unsigned int i;
  for(i=0;i<pw->n_PU;i++)
    proc_watch_update_pu(pw,i);
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

  if((pw->state = hwloc_bitmap_alloc())==NULL){
    perror("hwloc_bitmap_alloc()");
    free(pw); 
    exit(EXIT_FAILURE);
  }
  if((pw->query_state = hwloc_bitmap_alloc())==NULL){
    perror("hwloc_bitmap_alloc()");
    free(pw->state); 
    free(pw); 
    exit(EXIT_FAILURE);
  }
  if((pw->lock = calloc(pw->n_PU,sizeof(pthread_mutex_t)))==NULL){
    perror("calloc");
    free(pw->state); 
    free(pw->query_state);
    free(pw); 
    exit(EXIT_FAILURE);
  }

  for(i=0;i<pw->n_PU;i++){
    pthread_mutex_init(&(pw->lock[i]),NULL);
  }
  proc_watch_update(pw);
  return pw;
}

void
delete_proc_watch(struct proc_watch * pw){
  unsigned int i;
  if(pw==NULL)
    return;
  for(i=0;i<pw->n_PU;i++){
    pthread_mutex_destroy(&(pw->lock[i]));
    if(pw->tasks!=NULL)
      free(pw->tasks[i]);
  }
  hwloc_bitmap_free(pw->state);
  hwloc_bitmap_free(pw->query_state);
  free(pw->lock);
  if(pw->tasks!=NULL)
    free(pw->tasks);
  free(pw->p_dir_path);
  closedir(pw->p_dir);
  free(pw);
}

