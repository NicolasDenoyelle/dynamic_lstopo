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

int
hwloc_get_obj_depth_by_name(hwloc_topology_t topology, char * obj_name){
  /* find hwloc obj depth */
  hwloc_obj_type_t type;
  int depthattrp;
  hwloc_obj_cache_type_t cache_type;
  if(hwloc_obj_type_sscanf(obj_name,&type,&depthattrp,&cache_type,sizeof(cache_type))==-1){
    fprintf(stderr,"type \"%s\" was not recognized\n",obj_name);
    return -1;
  }
  int depth = hwloc_get_type_depth(topology,type);
  if(depth==HWLOC_TYPE_DEPTH_MULTIPLE){
    if(type==HWLOC_OBJ_CACHE){
      depth = hwloc_get_cache_type_depth(topology,depthattrp,cache_type);
      if(depth == HWLOC_TYPE_DEPTH_UNKNOWN){
	fprintf(stderr,"type %s cannot be found, level=%d\n",obj_name,depthattrp);
	return -1;
      }
      if(depth == HWLOC_TYPE_DEPTH_MULTIPLE){
	fprintf(stderr,"type %s multiple caches match for\n",obj_name);
	return -1;
      }
    }
    else{
      fprintf(stderr,"type \"%s\" isn't handled...\n",obj_name);
      return -1;
    }
  }
  return depth;
}

int
chk_input_file(const char * filename)
{
  if(access(filename, R_OK ) == -1 ){
    return 0;
  }
  return 1;
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


#define check_count(avail, count, max_count) do{	\
  if(count == max_count-1){				\
  max_count*=2;						\
  avail = realloc(avail,sizeof(*avail)*max_count);	\
  }							\
  } while(0)						\
    
char ** get_native_avail_papi_counters(unsigned * ncount){
  unsigned count=0, max_count = PAPI_MAX_HWCTRS + PAPI_MAX_PRESET_EVENTS;
  char ** avail = malloc(sizeof(char*)*max_count);
  int event_code = 0 | PAPI_NATIVE_MASK;
  PAPI_event_info_t info;
  int numcmp, cid;	
  int retval;

  /* native events */
  numcmp = PAPI_num_components();
  for(cid = 0; cid < numcmp; cid++){
    const PAPI_component_info_t *component;
    component=PAPI_get_component_info(cid);
    if (component->disabled) continue;
    retval = PAPI_enum_cmp_event(&event_code, PAPI_ENUM_FIRST, cid);
    if(retval==PAPI_OK){
      do{
	memset(&info, 0, sizeof(info));
	retval = PAPI_get_event_info(event_code, &info);
	if (retval != PAPI_OK) continue;
	printf("%s\n",info.symbol);
	avail[count]=strdup(info.symbol);
	check_count(avail,count,max_count);
	count++;
      } while(PAPI_enum_cmp_event(&event_code, PAPI_ENUM_EVENTS, cid) == PAPI_OK);
    }
  }
  *ncount=count;
  return avail;
}

char ** get_preset_avail_papi_counters(unsigned * ncount){
  PAPI_library_init( PAPI_VER_CURRENT);
  unsigned count=0, max_count = PAPI_MAX_HWCTRS + PAPI_MAX_PRESET_EVENTS;
  char ** avail = malloc(sizeof(char*)*max_count);
  int event_code = 0 | PAPI_PRESET_MASK;
  PAPI_event_info_t info;

  /* preset events */
  PAPI_enum_event( &event_code, PAPI_ENUM_FIRST );
  do {
    if ( PAPI_get_event_info( event_code, &info ) == PAPI_OK ) {
      printf("%s\n",info.symbol);
      avail[count]=strdup(info.symbol);
      count++;
    }
  } while (PAPI_enum_event( &event_code, PAPI_PRESET_ENUM_AVAIL ) == PAPI_OK);

  *ncount=count;
  return avail;
}


