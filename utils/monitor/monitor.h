#ifndef MONITOR_H
#define MONITOR_H

#include "hwloc.h"
#include <poll.h>

struct node_box{
  long long *     counters_val;
  double          val, old_val;
  double          max, min;
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

  void *            dlhandle;
  int               output_fd; 

  unsigned int      n_events;    
  char        **    event_names;      /* n_events * strlen(event_name) */

  unsigned int      n_PU;
  pthread_t       * pthreads;         /* nb_PU */
  struct node_box **PU_vals;          /* nb_PU */

  pthread_barrier_t barrier;
  pthread_cond_t    cond;
  pthread_mutex_t   cond_mtx, update_mtx;

  unsigned int      count;             /* nb_monitors */
  unsigned int      allocated_count; 
  char        **    names;             /* nb_monitors */
  double  (**compute)(long long *);    /* nb_monitors */
  unsigned int    * depths;            /* nb_monitors */
 };

typedef struct monitors * Monitors_t;


Monitors_t   new_default_Monitors             (hwloc_topology_t topology, const char * output, unsigned int pid);
Monitors_t   load_Monitors                    (hwloc_topology_t topology, const char * perf_group_file, const char * output, unsigned int pid);
unsigned int Monitors_watch_pid               (Monitors_t m,unsigned int pid);
int          Monitors_start                   (Monitors_t m);
void         Monitors_update_counters         (Monitors_t m);
void         Monitors_wait_update             (Monitors_t m);
long long    Monitors_get_counter_value       (Monitors_t m, unsigned int counter_idx, unsigned int PU_idx);
double       Monitors_get_monitor_max         (Monitors_t m, unsigned int m_idx);
double       Monitors_get_monitor_min         (Monitors_t m, unsigned int m_idx);
double       Monitors_get_monitor_value       (Monitors_t m, unsigned int node_idx, unsigned int PU_idx);
double       Monitors_get_monitor_variation   (Monitors_t m, unsigned int m_idx, unsigned int sibling_idx);
double       Monitors_wait_monitor_value      (Monitors_t m, unsigned int m_idx, unsigned int sibling_idx);
double       Monitors_wait_monitor_variation  (Monitors_t m, unsigned int m_idx, unsigned int sibling_idx);
void         delete_Monitors                  (Monitors_t m);
#endif
