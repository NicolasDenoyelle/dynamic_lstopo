#include "pwatch.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <search.h>
#include "hwloc/linux.h"
#include <linux/version.h>

struct task_stat{
  unsigned pu_num;
  unsigned state; // 1=RUNNING, 0=NOT RUNNING
};

struct proc_watch{
  unsigned int       pid;
  char             * p_dir_path;
  DIR              * p_dir;

  hwloc_bitmap_t     query_state;       /* physical */ /* 0 = must stop, 1 = must start */
  hwloc_bitmap_t     state;    /* physical */ /* 0 = stopped,   1 = started */
  hwloc_bitmap_t     tmp_state;
  pthread_mutex_t    mtx;
  struct task_stat * t_stat;
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
  //fprintf(stderr,"\t%s:pu_num=%d, pu_state=%c\n",task_path,t_info->pu_num,state);
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
    pthread_mutex_lock(&pw->mtx);
    hwloc_bitmap_set(pw->state,physical_PU);
    pthread_mutex_unlock(&pw->mtx);
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
    pthread_mutex_lock(&pw->mtx);
    hwloc_bitmap_clr(pw->state,physical_PU);
    pthread_mutex_unlock(&pw->mtx);
    return 1;
  }
  return 0;
}

inline int 
proc_watch_get_pu_state(struct proc_watch * pw, unsigned int physical_PU)
{
  return hwloc_bitmap_isset(pw->query_state,physical_PU);
}

inline void
proc_watch_get_watched_in_cpuset(struct proc_watch * pw, hwloc_cpuset_t cpuset, hwloc_cpuset_t out)
{
  if(pw!=NULL)
    hwloc_bitmap_and(out,cpuset,pw->query_state);
}

void 
proc_watch_update(struct proc_watch * pw)
{
  if(!pw)
    return;
  struct dirent * task_dir;
  char task_path[128];

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

  hwloc_bitmap_zero(pw->tmp_state);
  /* look into each pid thread to stat file */
  while((task_dir=readdir(pw->p_dir))!=NULL){
    if(!strcmp(task_dir->d_name,".") || !strcmp(task_dir->d_name,".."))
      continue;
    sprintf(task_path,"/proc/%d/task/%s/stat",pw->pid,task_dir->d_name);
    read_task(task_path,pw->t_stat);
    if(pw->t_stat->state)
      hwloc_bitmap_set(pw->tmp_state,pw->t_stat->pu_num);
  }
  pthread_mutex_lock(&pw->mtx);
  hwloc_bitmap_copy(pw->query_state, pw->tmp_state);
  pthread_mutex_unlock(&pw->mtx);
  rewinddir(pw->p_dir);
}


struct proc_watch *
new_proc_watch(unsigned int pid)
{
  int i;
  char tmp [11+strlen("/proc//task")];
  struct proc_watch * pw;
  pw = malloc(sizeof (struct proc_watch));
  if(pw==NULL){
    perror("malloc");
    exit(EXIT_FAILURE);
  }

  pw->pid=pid;
  sprintf(tmp,"/proc/%d/task",pid);
  pw->p_dir_path = strdup(tmp);
  pw->p_dir=NULL;

  if((pw->t_stat = malloc(sizeof(struct task_stat)))==NULL){
    perror("malloc");
    exit(EXIT_FAILURE);
  }

  if((pw->query_state = hwloc_bitmap_alloc())==NULL){
    perror("hwloc_bitmap_alloc()");
    exit(EXIT_FAILURE);
  }
  if((pw->state = hwloc_bitmap_alloc())==NULL){
    perror("hwloc_bitmap_alloc()");
    exit(EXIT_FAILURE);
  }

  if((pw->tmp_state = hwloc_bitmap_alloc())==NULL){
    perror("hwloc_bitmap_alloc()");
    exit(EXIT_FAILURE);
  }

  hwloc_bitmap_zero(pw->query_state);
  hwloc_bitmap_zero(pw->state);
  pthread_mutex_init(&pw->mtx,NULL);
  proc_watch_update(pw);
  return pw;
}

void
delete_proc_watch(struct proc_watch * pw){
  hwloc_bitmap_free(pw->query_state);
  hwloc_bitmap_free(pw->state);
  hwloc_bitmap_free(pw->tmp_state);
  free(pw->p_dir_path);
  free(pw->t_stat);
  closedir(pw->p_dir);
  pthread_mutex_destroy(&pw->mtx);
  free(pw);
}

