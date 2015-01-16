#ifndef PARSER_H
#define PARSER_H

#include "hwloc.h"

#define MONITOR_DOT_H "monitor.h"
#define MONITOR_DOT_C "src/monitor.c"
#define PARSED_CODE_SRC "/tmp/custom_monitors.c"
#define PARSED_CODE_LIB "/tmp/custom_monitors.so"

struct parsed_names{
  unsigned int n_events;
  char ** event_names;
  unsigned int n_monitors;
  char ** monitor_names;
  char ** monitor_obj;
};

int                   topology_init(hwloc_topology_t * topology);
char **               get_avail_papi_counters(unsigned * ncount);
char **               get_avail_hwloc_objs_names(unsigned * nobjs);
struct parsed_names * parser(const char * file_name);

#endif
