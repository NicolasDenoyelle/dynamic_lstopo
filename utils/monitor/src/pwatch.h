#ifndef PWATCH_H
#define PWATCH_H

#include <poll.h>
#include <hwloc.h>

#define MUST_STOP -2
#define STOPPED   -1
#define STARTED    1
#define MUST_START 2

struct task_info{
  int wd;
  int PU;
  char * name;
};

struct proc_watch{
  unsigned int       pid;
  char             * proc_dir_path;
  int                proc_wd;
  struct pollfd      notify_fd;
  int                nb_tasks ,alloc_tasks, n_PU;
  struct task_info * tinfo;             /* nb_tasks */
  int              * monitoring_state;  /* n_PU */
  int              * n_running;         /* n_PU */
};

struct proc_watch *  new_proc_watch   (unsigned int pid, unsigned int initial_count);
void                 delete_proc_watch(struct proc_watch * pw);
int                  proc_watch_update(struct proc_watch * pw);



#endif
