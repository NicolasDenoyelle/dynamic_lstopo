#ifndef PWATCH_H
#define PWATCH_H

/**
 * methods are thread_safe.
 */

#include <hwloc.h>


struct proc_watch;
struct proc_watch *  new_proc_watch(hwloc_topology_t topology, unsigned int pid, unsigned int initial_count);
void                 delete_proc_watch(struct proc_watch * pw);

void                 proc_watch_add_task(struct proc_watch * pw, unsigned int tid);
void                 proc_watch_add_task_on_pu(struct proc_watch * pw, unsigned int tid, unsigned int physical_PU);
int                  proc_watch_rm_task(struct proc_watch * pw, unsigned int tid);
int                  proc_watch_rm_task_on_pu(struct proc_watch * pw, unsigned int tid, unsigned int physical_PU);

void                 proc_watch_update(struct proc_watch * pw);
void                 proc_watch_update_tasks(struct proc_watch * pw);
void                 proc_watch_update_pu(struct proc_watch * pw, int physical_pu);
hwloc_bitmap_t       proc_watch_get_watched_in_cpuset(struct proc_watch * pw, hwloc_cpuset_t cpuset);


/******************* PRIVATE BUT USED IN monitor.c **************************************************************/
int                  logical_to_physical(hwloc_topology_t topology, int logical_idx);
int                  physical_to_logical(hwloc_topology_t topology, int physical_idx);
int                  proc_watch_check_start_pu(struct proc_watch * pw,unsigned int physical_PU);
int                  proc_watch_check_stop_pu(struct proc_watch * pw, unsigned int physical_PU);
int                  proc_watch_get_pu_state(struct proc_watch * pw, unsigned int physical_PU);


#endif
