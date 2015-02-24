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
struct line_content{
  unsigned phase;
  unsigned sibling_idx;
  char obj_name[11];
  long long real_usec;
  char name[21];
  double value;
};

ssize_t               input_line_content(FILE * in, struct line_content * out);
void                  output_header(int output_fd);
void                  output_line_content(int output_fd, struct line_content * in);
/***************************/

/* utils from list_avail.c */
int                   topology_init(hwloc_topology_t * topology);
int                   chk_input_file(const char * filename);
pid_t                 start_executable(char * executable, char * exe_args[]);
int                   hwloc_get_obj_depth_by_name(hwloc_topology_t topology, char * obj_name);
char **               get_avail_papi_counters(unsigned * ncount);
char **               get_avail_hwloc_objs_names(unsigned * nobjs);


#endif
