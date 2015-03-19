#ifndef MONITOR_H
#define MONITOR_H

#include "hwloc.h"

/*************************************************************/
/**
 * @brief Struct stored in hwloc_obj_t->userdata.
 * It contains informations about hardware counters.
 * When building monitors_t every sibling at depths defined by hwloc_obj_types will store one of it.
 **/
struct monitor_node{
  /**
   * The aggregated counters value.
   **/
  long long *     counters_val;
  /**
   * The monitor node value computed from aggregated counters value.
   * val = current, val1 = old val, val2 = old old val.
   **/
  double val;
  double val1;
  double val2; 
  /**
   * The average timestamp at chich the counters were read.
   **/
  long long       real_usec, old_usec;
  /**
   * The number of leaves which updated counters_val. The node is uptodate when this value has reached the actual active leaf PU number.
   **/
  int             uptodate;
  /**
   * Locked while not uptodate.
   **/
  pthread_mutex_t read_lock;
  /**
   * Locked while a leaf aggregation is occuring.
   **/
  pthread_mutex_t update_lock;
  /**
   * Node identifiers in topology.
   **/
  unsigned depth,sibling,id;
};
/*************************************************************/


/*************************************************************/
/**
 * @brief The struct to manipulate topological monitoring.
 **/
struct monitors{
  /**
   * The machine topology where are stored the counters.
   */
  hwloc_topology_t  topology;
  /**
   * A structure which holds a PU bitmap with bit set to 1 when <pid>'s children task's state is running on this PU.
   */
  struct proc_watch *pw;

  /**
   * The dynamic library handle to dynamically load compute functions parsed and compiled from an input file.
   **/
  void *            dlhandle;
  /**
   * The file descriptor to the output file where to write the trace.
   **/
  int               output_fd; 
  /**
   * The number of PAPI event to read.
   **/
  unsigned int      n_events;    
  /**
   * The PAPI event names to read
   * size = n_events.
   **/
  char        **    event_names;      /* n_events * strlen(event_name) */
  /**
   * The number of PU to read
   **/
  unsigned int      n_PU;
  /**
   * One thread per PU to read local counters.
   **/
  pthread_t       * pthreads;         /* nb_PU */
  /**
   * One node per PU to store read values.
   **/
  struct monitor_node **PU_vals;      /* nb_PU */
  /**
   * A barrier to assert every thread read counters simultaneously when calling pthread_cond_broadcast from Monitors_update_counters. 
   **/
  pthread_barrier_t barrier;
  /**
   * Lock for cond, lock to wait until each monitor nodes is uptodate, lock for threads which have to print to trace file. 
   **/
  pthread_mutex_t   print_mtx;
  /**
   * The number of monitors described. Only one depth per monitor is accepted. 
   **/
  unsigned int      count;             /* nb_monitors */
  /**
   * When adding monitors we use realloc and optimize its use by doubling its size each time it is necessary.
   **/
  unsigned int      allocated_count; 
  /**
   * An array to old each monitor name. Indexes are the same as depths array, depth_names array, and max and min arrays.
   **/
  char        **    names;             /* nb_monitors */
  /**
   * An array of function which compute the monitor value from the hardware counters array of values.
   */
  double  (**compute)(long long *);    /* nb_monitors */
  /**
   * An array to old each monitor depth.
   **/
  unsigned int    * depths;            /* nb_monitors */
  /**
   * An array to hold monitors depth names.
   */
  char        **    depth_names;       /* nb_monitors */
  /**
   * Two arrays to store min and max monitors value at each depth.
   */
  double           *max, *min;         /* nb_monitors */
  /**
   * An integer printed to trace file used to replay a part of the recorded execution.
   */
  int phase;
  /**
   * The number of monitor nodes stored in topology.
   **/
  unsigned int      n_nodes;

 };
typedef struct monitors * monitors_t;
/*************************************************************/


/*************************************************************/
/**
 * @brief Instanciate monitors. 
 * @param topology
 *        The topology to use. If NULL, a new topology is created from the local computer.
 * @param output
 *        The filename where to write the trace.
 * @return New monitors
 */
monitors_t   new_default_Monitors             (hwloc_topology_t topology, const char * output);
/*************************************************************/


/*************************************************************/
/**
 * @brief Instanciate monitors. 
 * @param topology
 *        The topology to use. If NULL, a new topology is created from the local computer.
 * @param perf_group_file
 *        The filename to parse in order to use custom monitors.
 * @param output
 *        The filename where to write the trace.
 * @return New monitors
 */
monitors_t   load_Monitors_from_config        (hwloc_topology_t topology, const char * perf_group_file, const char * output);
/*************************************************************/


/*************************************************************/
/**
 * @brief Free resources allocated in monitors_t
 * @param m
 *        The monitors to free.
 */
void         delete_Monitors                  (monitors_t m);
/*************************************************************/


/*************************************************************/
/**
 * @brief Record exection only for a chosen processus. Each call to Monitors_update_counters will induce a look in /proc/<pid>/task to list pid 
 * subtasks.
 * Each subtask state and location is saved to bitmap arrays and then used by update threads to know if they have to start or stop reading 
 * counters.
 * @param m
 *        An instanciated monitors_t.
 * @param pid
 *        The program pid to watch. 
 * @return -1 if an error occured, the old pid watched if a pid wasalready watched, else 0.
 */
unsigned int Monitors_watch_pid               (monitors_t m,unsigned int pid);
/*************************************************************/


/*************************************************************/
/**
 * @brief Spawns reading threads, initializes eventsets and starts PAPI_counters, then wait in a loop to be updated.
 * @param m
 *        The monitors to start.
 * @return Always return 0 and exit on failure.
 */
int          Monitors_start                   (monitors_t m);
/*************************************************************/


/*************************************************************/
/**
 * @brief If a Monitors_watch_pid has been called, it look status and location of pid tasks, then it triggers pthread_cond_broadcast, which unlock
 * threads to immediately read counters.
 * @param m
 *        The monitors to update.
 */
void         Monitors_update_counters         (monitors_t m);
/*************************************************************/


/*************************************************************/
/**
 * @brief Waits until every monitor node into the topology is uptodate.
 * @param m
 *        The monitors to wait for.
 */
void         Monitors_wait_update             (monitors_t m);
/*************************************************************/


/*************************************************************/
/**
 * @brief When using lstopo to replay an execution from trace, the phase ca be set to analyse only parts of execution.
 * @param m
 *        The monitors to which has to be set.
 * @param phase
 *        An unsigned containing the phase id.
 */
void         Monitors_set_phase(monitors_t m, unsigned phase);
/*************************************************************/


/*************************************************************/
/**
 * @brief Get the max value of a monitor at a given depth: depths[level_idx].
 * @param m
 *        The monitors which max value has to be retrieved.
 * @param level_idx
 *        An unsigned into [[0:m->count]] to identify a monitor.
 * @return The max value of a given monitor.
 */
double       Monitors_get_level_max           (monitors_t m, unsigned int level_idx);
/*************************************************************/


/*************************************************************/
/**
 * @brief Get the min value of a monitor at a given depth: depths[level_idx].
 * @param m
 *        The monitors which min value has to be retrieved.
 * @param level_idx
 *        An unsigned into [[0:m->count]] to identify a monitor.
 * @return The min value of a given monitor.
 */
double       Monitors_get_level_min           (monitors_t m, unsigned int level_idx);
/*************************************************************/


/*************************************************************/
/**
 * @brief Get a monitor node value.
 * @param m
 *        The monitors which value has to be retrieved.
 * @param depth
 *        The monitor depth which value has to be retrieved.
 * @param sibling_idx
 *        The monitor logical index int topology, which value has to be retrieved.
 * @return The value of a given monitor.
 */
double       Monitors_get_monitor_value       (monitors_t m, unsigned int depth, unsigned int sibling_idx);
/*************************************************************/


/*************************************************************/
/**
 * @brief Get the difference between the last value of a monitor and its previous one.
 * @param m
 *        The monitors which value has to be retrieved.
 * @param depth
 *        The monitor depth which value has to be retrieved.
 * @param sibling_idx
 *        The monitor logical index int topology, which value has to be retrieved.
 * @return The max variation of a given monitor.
 */
double       Monitors_get_monitor_variation   (monitors_t m, unsigned int depth, unsigned int sibling_idx);
/*************************************************************/


/*************************************************************/
/**
 * @brief Wait only for a single monitor value to be uptodate and retrieve it.
 * @param m
 *        The monitors which value has to be retrieved.
 * @param depth
 *        The monitor depth which value has to be retrieved.
 * @param sibling_idx
 *        The monitor logical index int topology, which value has to be retrieved.
 * @return The value of a given monitor.
 */
double       Monitors_wait_monitor_value      (monitors_t m, unsigned int depth, unsigned int sibling_idx);
/*************************************************************/


/*************************************************************/
/**
 * @brief Wait only for a single monitor to be uptodate and retrieve the difference between its new value and the old one.
 * @param m
 *        The monitors which variation has to be retrieved.
 * @param depth
 *        The monitor depth which variation has to be retrieved.
 * @param sibling_idx
 *        The monitor logical index int topology, which variation has to be retrieved.
 * @return The variation of a given monitor.
 */
double       Monitors_wait_monitor_variation  (monitors_t m, unsigned int depth, unsigned int sibling_idx);
/*************************************************************/
#endif
