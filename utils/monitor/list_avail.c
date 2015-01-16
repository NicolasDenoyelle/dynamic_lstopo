#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <papi.h>
#include "hwloc.h"

int topology_init(hwloc_topology_t * topology){
  hwloc_topology_init(topology); 
  hwloc_topology_set_flags(*topology, HWLOC_TOPOLOGY_FLAG_WHOLE_SYSTEM | HWLOC_TOPOLOGY_FLAG_ICACHES);
  hwloc_topology_load(*topology);
}

char ** get_avail_hwloc_objs_names(unsigned * nobjs){
  int depth;
  hwloc_obj_t obj;
  char ** obj_types;
  char obj_type[128]; 
  hwloc_topology_t topology;
 
  topology_init(&topology);
  
  depth = hwloc_topology_get_depth(topology);
  obj_types=malloc(sizeof(char*)*depth);
  int i,index=0;
  
  obj = hwloc_get_root_obj(topology);
  do{
    memset(obj_type,0,128);
    hwloc_obj_type_snprintf(obj_type, 128, obj, 0);

    for(i=0;i<index;i++){
      if(!strcmp(obj_type,obj_types[i])){
	break;
      }
    }
    if(i==index){
      obj_types[index] = strdup(obj_type);
      index++;
    }
  } while((obj=hwloc_get_next_child(topology,obj,NULL))!=NULL);

  *nobjs=index;
  hwloc_topology_destroy(topology);
  return obj_types;
}


char ** get_avail_papi_counters(unsigned * ncount){
  PAPI_library_init( PAPI_VER_CURRENT);
  unsigned count=0, max_count = PAPI_MAX_HWCTRS + PAPI_MAX_PRESET_EVENTS;
  char ** avail = malloc(sizeof(char*)*max_count);
  int event_code = 0 | PAPI_PRESET_MASK;
  PAPI_event_info_t info;

  PAPI_enum_event( &event_code, PAPI_ENUM_FIRST );
  do {
    if ( PAPI_get_event_info( event_code, &info ) == PAPI_OK ) {
      avail[count]=strdup(info.symbol);
      count++;
    }
  } while (PAPI_enum_event( &event_code, PAPI_PRESET_ENUM_AVAIL ) == PAPI_OK);
  *ncount=count;
  return avail;
}


