#ifndef PWATCH_H
#define PWATCH_H

#include "hwloc.h"

struct proc_watch;
struct proc_watch *  new_proc_watch                  (unsigned int pid);
void                 delete_proc_watch               (struct proc_watch * pw);
void                 proc_watch_update               (struct proc_watch * pw);
int                  proc_watch_check_start_pu       (struct proc_watch * pw,unsigned int physical_PU);
int                  proc_watch_check_stop_pu        (struct proc_watch * pw, unsigned int physical_PU);
int                  proc_watch_get_pu_state         (struct proc_watch * pw, unsigned int physical_PU);
void                 proc_watch_get_watched_in_cpuset(struct proc_watch * pw, hwloc_cpuset_t cpuset, hwloc_cpuset_t out);
unsigned int         proc_watch_get_pid              (struct proc_watch * pw);

/******************* PRIVATE BUT USED IN monitor.c **************************************************************/
int                  logical_to_physical(hwloc_topology_t topology, int logical_idx);
int                  physical_to_logical(hwloc_topology_t topology, int physical_idx);

#endif
