#ifndef PWATCH_H
#define PWATCH_H

/**
 * methods are thread_safe.
 */

#include <hwloc.h>

struct proc_watch;

struct proc_watch *  new_proc_watch(hwloc_topology_t * topo, unsigned int pid, unsigned int initial_count);
void                 delete_proc_watch(struct proc_watch * pw);

void                 proc_watch_add_task(struct proc_watch * pw, unsigned int tid);
void                 proc_watch_add_task_on_pu(struct proc_watch * pw, unsigned int tid, unsigned int logical_PU);
int                  proc_watch_rm_task(struct proc_watch * pw, unsigned int tid);
int                  proc_watch_rm_task_on_pu(struct proc_watch * pw, unsigned int tid, unsigned int logical_PU);

void                 proc_watch_update(struct proc_watch * pw);
void                 proc_watch_update_pu(struct proc_watch * pw, int logical_pu);

int                  proc_watch_check_start_pu(struct proc_watch * pw,unsigned int logical_PU);
int                  proc_watch_check_stop_pu(struct proc_watch * pw, unsigned int logical_PU);
int                  proc_watch_get_pu_state(struct proc_watch * pw, unsigned int logical_PU);
hwloc_bitmap_t       proc_watch_get_watched_pu_in_cpuset(struct proc_watch * pw, hwloc_cpuset_t cpuset);

#endif
