#ifndef MONITOR_H
#define MONITOR_H

#include "hwloc.h"
typedef struct monitors * Monitors_t;

Monitors_t new_default_Monitors             (hwloc_topology_t topology, 
					       const char * output, 
					       unsigned int pid);
Monitors_t load_Monitors                    (hwloc_topology_t topology,
					       const char * perf_group_file, 
					       const char * output, 
					       unsigned int pid);

int        Monitors_start                   (Monitors_t m);

void       Monitors_update_counters         (Monitors_t m);
void       Monitors_wait_update             (Monitors_t m);

long long  Monitors_get_counter_value       (Monitors_t m,
					       unsigned int counter_idx,
					       unsigned int PU_idx);
double     Monitors_get_monitor_max         (Monitors_t m, 
					       unsigned int m_idx);
double     Monitors_get_monitor_min         (Monitors_t m, 
					       unsigned int m_idx);
double     Monitors_get_monitor_value       (Monitors_t m, 
					       unsigned int node_idx, 
					       unsigned int PU_idx);
double     Monitors_get_monitor_variation   (Monitors_t m, 
					       unsigned int m_idx, 
					       unsigned int sibling_idx);
double     Monitors_wait_monitor_value      (Monitors_t m, 
					       unsigned int m_idx, 
					       unsigned int sibling_idx);
double     Monitors_wait_monitor_variation  (Monitors_t m, 
					       unsigned int m_idx, 
					       unsigned int sibling_idx);
void       Monitors_print                   (Monitors_t m);
void       Monitors_insert_print_in_topology(Monitors_t m);
void       Monitors_print_in_topology       (Monitors_t m);


void       delete_Monitors                  (Monitors_t m);
#endif
