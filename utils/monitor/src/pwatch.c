#include "pwatch.h"
#include <sys/inotify.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hwloc.h>

#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + 16))

int  comparetinfo(const void* a,const void* b ){return ((struct task_info *)a)->wd - ((struct task_info*)b)->wd;}

int
read_task_core(const char * task_path)
{
  FILE * task = fopen(task_path, "r");
  if(task==NULL)
    return -1;
  char c, pu_num[11]; int i=38;
  memset(pu_num,0,11);
  /* move cursor to PU id */
  for(i=0;i<38;i++)
    while((c=fgetc(task))!=' ');
  /* read PU id */
  while((c=fgetc(task))!=' ')
    strcat(pu_num,&c);
  fclose(task);
  return atoi(pu_num);
}


struct proc_watch *
new_proc_watch(unsigned int pid, unsigned int n_tasks)
{
  struct proc_watch * pw = malloc(sizeof (struct proc_watch));
  if(pw==NULL){
    perror("malloc");
    return NULL;
  }
  
  /* get n_PU */
  hwloc_topology_t topo;
  hwloc_topology_init(&topo);
  hwloc_topology_load(topo);
  pw->n_PU=hwloc_get_nbobjs_by_depth(topo,hwloc_topology_get_depth(topo)-1);
  
  /* initialize notify and poll */
  if((pw->notify_fd.fd=inotify_init())==0){
    perror("inotify_init");
    free(pw);
    return NULL;
  }
  pw->notify_fd.events=POLLIN;

  pw->alloc_tasks = 32;
  char tmp [11+strlen("/proc//task")];
  sprintf(tmp,"/proc/%d/task",pid);
  pw->proc_dir_path = strdup(tmp);
  pw->nb_tasks=0;
  pw->pid=pid;
  
  /* alloc space */
  if((pw->monitoring_state = calloc(pw->n_PU,sizeof(int)))==NULL){
    perror("calloc");
    close(pw->notify_fd.fd); 
    free(pw); 
    return NULL;
  }

  if((pw->n_running = calloc(pw->n_PU,sizeof(int)))==NULL){
    perror("calloc");
    close(pw->notify_fd.fd); 
    free(pw->monitoring_state); 
    free(pw); 
    return NULL;
  }
  
  if((pw->tinfo = calloc(pw->alloc_tasks,sizeof(struct task_info)))==NULL){
    perror("calloc");
    close(pw->notify_fd.fd);
    free(pw->monitoring_state); 
    free(pw->n_running); 
    free(pw); 
    return NULL;
  }
  
  int i;
  for(i=0;i<pw->n_PU;i++){
    pw->monitoring_state[i]=STOPPED;
    pw->n_running[i]=0;
  }
  pw->proc_wd = ENOENT;
  return pw;
}

struct task_info proc_watch_remove_task(struct proc_watch * pw, int task_wd);

void
delete_proc_watch(struct proc_watch * pw){
  if(pw==NULL)
    return;
  unsigned int i;
  for(i=0;i<pw->nb_tasks; i++){
    proc_watch_remove_task(pw,pw->tinfo[i].wd);
  }
  free(pw->tinfo);
  free(pw->monitoring_state);
  free(pw->n_running); 
  free(pw->proc_dir_path);
  inotify_rm_watch(pw->notify_fd.fd,pw->proc_wd);
}


int
proc_watch_add_task(struct proc_watch * pw, char * task_id, uint32_t mask)
{
  if(pw->alloc_tasks<=pw->nb_tasks){
    pw->alloc_tasks*=2;
    if((pw->tinfo = realloc(pw->tinfo,sizeof(struct task_info)*pw->alloc_tasks))==NULL){
      perror("realloc");
      delete_proc_watch(pw); return -1;
    }
  }

  char task_path[strlen(pw->proc_dir_path)+strlen("//stat")+strlen(task_id)];
  sprintf(task_path,"%s/%s/stat",pw->proc_dir_path,task_id);
  int pu_n=read_task_core(task_path);
  pw->tinfo[pw->nb_tasks].PU = pu_n;
  pw->tinfo[pw->nb_tasks].wd = inotify_add_watch(pw->notify_fd.fd,task_path,mask);
  if((pw->tinfo[pw->nb_tasks]).wd==-1){
    fprintf(stderr,"inotify_add_watch: %s\n",strerror(errno));
    return -1;
  }

  (pw->tinfo[pw->nb_tasks]).name = strdup(task_id);
  if((pw->n_running[pu_n]++)== 0) pw->monitoring_state[pu_n]=MUST_START;
  pw->nb_tasks++;
  qsort(pw->tinfo,pw->nb_tasks,sizeof(struct task_info),comparetinfo);
  printf("%s watched\n",task_path);
  return 0;
}


struct task_info
proc_watch_remove_task(struct proc_watch * pw, int task_wd)
{
  struct task_info key = {task_wd,0};
  struct task_info * tinfo = bsearch(&key,pw->tinfo,pw->nb_tasks,sizeof(struct task_info),comparetinfo);
  if(tinfo==NULL){
    if(task_wd==pw->proc_wd){
      delete_proc_watch(pw);
    }
    return key;
  }
  key.PU=(*tinfo).PU;
  free((*tinfo).name);
  unsigned int idx = (tinfo-pw->tinfo)/sizeof(struct task_info);
  inotify_rm_watch(pw->notify_fd.fd,task_wd);
  memcpy(&(pw->tinfo[idx]),&(pw->tinfo[idx+1]),sizeof(struct task_info)*(pw->nb_tasks-idx-1));
  pw->nb_tasks--;
  qsort(pw->tinfo,pw->nb_tasks,sizeof(struct task_info),comparetinfo);

  /* there is no remaining task on the PU */
  if((--pw->n_running[key.PU])==0) pw->monitoring_state[key.PU] = MUST_STOP;
  return key;
}


int
proc_watch_move_task(struct proc_watch * pw, int task_wd)
{
  struct task_info key = {task_wd,0};
  struct task_info * tinfo = bsearch(&key,pw->tinfo,pw->nb_tasks,sizeof(struct task_info),comparetinfo);
  if(tinfo==NULL){
    return -1;
  }
  char task_path[strlen(pw->proc_dir_path)+strlen("//stat")+strlen(tinfo->name)];
  sprintf(task_path,"%s/%s/stat",pw->proc_dir_path,tinfo->name);
  int pu_n=read_task_core(task_path);
  
  /* check if a task is still running on old PU */
  if((--pw->n_running[tinfo->PU])==0) pw->monitoring_state[tinfo->PU] = MUST_STOP;
  /* check if a task is already running on new PU */
  if((pw->n_running[pu_n]++)== 0) pw->monitoring_state[pu_n]=MUST_START;
  printf("%s moved from PU:%d to PU:%d\n",tinfo->name,tinfo->PU,pu_n);
  tinfo->PU=pu_n;
  return 0;
}

int
proc_watch_update(struct proc_watch * pw)
{
  if(pw==NULL)
    return -1;
  /* check if pid is already running */
  if(pw->proc_wd==ENOENT){
    pw->proc_wd = inotify_add_watch(pw->notify_fd.fd,pw->proc_dir_path,IN_CREATE|IN_DELETE_SELF);
    if(pw->proc_wd==ENOENT) return -1;
    char pid[11]; sprintf(pid,"%d",pw->pid);
    proc_watch_add_task(pw,pid,IN_MODIFY|IN_DELETE_SELF);
  }
  /* poll notify_fd */
  if(poll(&(pw->notify_fd),1,0)>0){
      printf("event in %s\n",pw->proc_dir_path);
      char buffer[BUF_LEN];
      unsigned int len, i=0;
      len = read(pw->notify_fd.fd,buffer,BUF_LEN);
      if(len<0)
	perror("read");
      while(i<len){
	struct inotify_event * event = (struct inotify_event *) &buffer[i];
	if(event->len){

	  if (event->mask & IN_DELETE_SELF){
	    proc_watch_remove_task(pw,event->wd);
	    return 0;
	  }

	  if (event->mask & IN_MODIFY){
	    proc_watch_move_task(pw,event->wd);
	    i += EVENT_SIZE + event->len;
	    continue;
	  }

	  if (event->mask & IN_CREATE){
	    proc_watch_add_task(pw,event->name,IN_MODIFY|IN_DELETE_SELF);
	    i += EVENT_SIZE + event->len;
	    continue;
	  }
      }
    }   
  }
  return 0;
}



