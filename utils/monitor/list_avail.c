#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
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


pid_t 
start_executable(char * executable, char * exe_args[])
{
  pid_t ret=0;
  pid_t *child = mmap(NULL, sizeof *child, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  *child=0;
  pid_t pid2, pid1 = fork();
  if(pid1){
    wait(NULL);
  }
  else if(!pid1){
    pid2=fork();
    if(pid2){
      *child = pid2;
      exit(0);
    }
    else if(!pid2){
      ret = execvp(executable, exe_args);
      if (ret) {
	fprintf(stderr, "Failed to launch executable \"%s\"\n",
		executable);
	perror("execvp");
	exit(EXIT_FAILURE);
      }
    }
  }
  msync(child, sizeof(*child), MS_SYNC);
  ret = *child;
  munmap(child, sizeof *child);
  return ret; 
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


