#ifndef MONITOR_H
#define MONITOR_H

#include <stdio.h>

typedef struct monitors * Monitors_t;

int        Monitors_init();

Monitors_t new_default_Monitors       (const char * output, unsigned int pid);
Monitors_t load_Monitors              (const char * perf_group_file, 
				       const char * output,
				       unsigned int pid);
int        Monitors_start             (Monitors_t m);
void       Monitors_update_counters   (Monitors_t m);
void       Monitors_wait_update       (Monitors_t m);
long long  Monitors_get_counter_value (Monitors_t m,
				       unsigned int counter_idx,
				       unsigned int PU_idx);
double     Monitors_get_monitor_value (Monitors_t m, 
				       unsigned int node_idx, 
				       unsigned int PU_idx);
double     Monitors_wait_monitor_value(Monitors_t m, 
				       unsigned int m_idx, 
				       unsigned int sibling_idx);
void       Monitors_print             (Monitors_t m);

void       delete_Monitors            (Monitors_t m);
#endif
