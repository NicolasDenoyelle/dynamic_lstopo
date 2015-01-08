#ifndef PARSER_H
#define PARSER_H

#define MONITOR_DOT_H "monitor.h"
#define MONITOR_DOT_C "src/monitor.c"
#define PARSED_CODE_SRC "/tmp/custom_monitors.c"
#define PARSED_CODE_LIB "/tmp/custom_monitors.so"

struct parsed_names{
  unsigned int n_events;
  char ** event_names;
  unsigned int n_monitors;
  char ** monitor_names;
  unsigned int * depths;
};

struct parsed_names * parser(const char * file_name);

#endif
