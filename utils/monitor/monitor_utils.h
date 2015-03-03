#ifndef M_UTILS_H
#define M_UTILS_H

#include "monitor.h"
#include "pwatch.h"

#define PARSED_CODE_SRC "/tmp/custom_monitors.c"
#define PARSED_CODE_LIB "/tmp/custom_monitors.so"

/* utils from parser.y */
struct parsed_names{
  unsigned int n_events;
  char ** event_names;
  unsigned int n_monitors;
  char ** monitor_names;
  char ** monitor_obj;
};

struct parsed_names * parser(const char * file_name);
/***********************/

/* utils from replay.c */
struct header_line{
  int id;
  char level[10];
  unsigned sibling;
};

struct value_line{
  int id;
  unsigned phase;
  long long real_usec;
  double value;
};


void                  output_header_paje(monitors_t m);
void                  output_line_content_paje(int output_fd, struct value_line * in);
/***************************/

/* utils from list_avail.c */
int                   topology_init(hwloc_topology_t * topology);
int                   chk_input_file(const char * filename);
pid_t                 start_executable(char * executable, char * exe_args[]);
int                   hwloc_get_obj_depth_by_name(hwloc_topology_t topology, char * obj_name);
char **               get_avail_papi_counters(unsigned * ncount);
char **               get_avail_hwloc_objs_names(unsigned * nobjs);


#endif
