#ifndef MONITOR_H
#define MONITOR_H

#include "hwloc.h"
#include <poll.h>

struct monitor_node{
  long long *     counters_val;
  double          val, old_val;
  long long       real_usec, old_usec;
  int             uptodate;
  pthread_mutex_t read_lock;
  pthread_mutex_t update_lock;
};

struct monitors{
  /**
   * In hwloc topology tree at each monitor depth, every sibling stores a struct monitor_node in its userdata
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
  struct monitor_node **PU_vals;          /* nb_PU */

  pthread_barrier_t barrier;
  pthread_cond_t    cond;
  pthread_mutex_t   cond_mtx, update_mtx, print_mtx;

  unsigned int      count;             /* nb_monitors */
  unsigned int      allocated_count; 
  char        **    names;             /* nb_monitors */
  double  (**compute)(long long *);    /* nb_monitors */
  unsigned int    * depths;            /* nb_monitors */
  double          *max, *min;          /* nb_monitors */
 };

typedef struct monitors * monitors_t;


monitors_t   new_default_Monitors             (hwloc_topology_t topology, const char * output, unsigned int pid);
monitors_t   load_Monitors_from_config        (hwloc_topology_t topology, const char * perf_group_file, const char * output, unsigned int pid);
unsigned int Monitors_watch_pid               (monitors_t m,unsigned int pid);
int          Monitors_start                   (monitors_t m);
void         Monitors_update_counters         (monitors_t m);
void         Monitors_wait_update             (monitors_t m);
long long    Monitors_get_counter_value       (monitors_t m, unsigned int counter_idx, unsigned int PU_idx);
double       Monitors_get_monitor_max         (monitors_t m, unsigned int m_idx);
double       Monitors_get_monitor_min         (monitors_t m, unsigned int m_idx);
double       Monitors_get_monitor_value       (monitors_t m, unsigned int node_idx, unsigned int PU_idx);
double       Monitors_get_monitor_variation   (monitors_t m, unsigned int m_idx, unsigned int sibling_idx);
double       Monitors_wait_monitor_value      (monitors_t m, unsigned int m_idx, unsigned int sibling_idx);
double       Monitors_wait_monitor_variation  (monitors_t m, unsigned int m_idx, unsigned int sibling_idx);
void         delete_Monitors                  (monitors_t m);
#endif
