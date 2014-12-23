#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <papi.h>
#include <unistd.h>
#include "monitor.h"

void print_usage(char * program){
  fprintf(stderr, "Usage: %s [-h] help [-i] input_file [-o] output_file [-p] pid [-r] refresh_usec\n",
	  program);
  exit(EXIT_SUCCESS);
}

int
main(int argc, char** argv)
{
  char * in=NULL, * out=NULL;
  int opt,pid=0;
  if(argc==2 && !strcmp("help",argv[1])){
    print_usage(argv[0]);
  }
  struct timeval refresh; refresh.tv_usec=0;
  while((opt=getopt(argc,argv,"r:i:p:t:h"))!=-1){
    switch(opt){
    case 'i':
      in = strdup(optarg);
      break;
    case 'o':
      out = strdup(optarg);
      break;
    case 'p':
      pid = atoi(optarg);
      break;
    case 'r':
      refresh.tv_usec=atoi(optarg);
      break;
    case 'h':
      print_usage(argv[0]);
      break;
    default:
      print_usage(argv[0]);
      break;
    }
  }

  hwloc_topology_t topology;
  unsigned long flags = HWLOC_TOPOLOGY_FLAG_IO_DEVICES | HWLOC_TOPOLOGY_FLAG_ICACHES;
  hwloc_topology_init(&(topology));
  hwloc_topology_set_flags(topology, flags);
  hwloc_topology_load(topology);

  /* creating monitors */
  Monitors_t m = load_Monitors(topology,in,out,pid);
  if(m==NULL){
    /* default monitors creation */
    m = new_default_Monitors(topology,out,pid);
  }
  unsigned int i;

  if(m==NULL){
    return EXIT_FAILURE;
  }
  if(in)  free(in);
  if(out) free(out);
  Monitors_start(m);

  i = 4;
  if(refresh.tv_usec==0)
    while(i--)
      {
	Monitors_update_counters(m);
	Monitors_wait_update(m);
	Monitors_print(m);
      }
  else
    while(1){
      Monitors_update_counters(m);
      usleep(refresh.tv_usec);
      Monitors_wait_update(m);
      Monitors_print(m);
    }

  delete_Monitors(m);
  hwloc_topology_destroy(topology);
  return 0;
}

