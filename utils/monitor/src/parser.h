#ifndef PARSER_H
#define PARSER_H

#define MONITOR_DOT_H "monitor.h"
#define MONITOR_DOT_C "src/monitor.c"
#define PARSED_CODE_SRC "src/custom_monitors.c"
#define PARSED_CODE_LIB "build/custom_monitors.so"

int parser(const char * file_name);

#endif
