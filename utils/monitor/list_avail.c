#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <papi.h>
#include "hwloc.h"
#include "monitor_utils.h"

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


static int
parse_unit_masks( PAPI_event_info_t * info )
{
	char *pmask,*ptr;

	/* handle the PAPI component-style events which have a component:::event type */
	if ((ptr=strstr(info->symbol, ":::"))) {
		ptr+=3;
		/* handle libpfm4-style events which have a pmu::event type event name */
	} else if ((ptr=strstr(info->symbol, "::"))) {
		ptr+=2;
	}
	else {
		ptr=info->symbol;
	}

	if ( ( pmask = strchr( ptr, ':' ) ) == NULL ) {
		return ( 0 );
	}
	memmove( info->symbol, pmask, ( strlen(pmask) + 1 ) * sizeof(char) );

	//  The description field contains the event description followed by a tag 'masks:'
	//  and then the mask description (if there was a mask with this event).  The following
	//  code isolates the mask description part of this information.

	pmask = strstr( info->long_descr, "masks:" );
	if ( pmask == NULL ) {
		info->long_descr[0] = 0;
	} else {
		pmask += 6;        // bump pointer past 'masks:' identifier in description
		memmove( info->long_descr, pmask, (strlen(pmask) + 1) * sizeof(char) );
	}
	return ( 1 );
}
    
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
	if (check_papi_counter(info.symbol) != PAPI_OK) continue;
	avail[count]=strdup(info.symbol);
	check_count(avail,count,max_count);
	count++;
      } while(PAPI_enum_cmp_event(&event_code, PAPI_ENUM_ALL, cid) == PAPI_OK);
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
      avail[count]=strdup(info.symbol);
      count++;
    }
  } while (PAPI_enum_event( &event_code, PAPI_PRESET_ENUM_AVAIL ) == PAPI_OK);

  *ncount=count;
  return avail;
}

void dump_avail(char * (*get_avail(unsigned *)))
{
  unsigned n_avail, i;
  char *ptr, ** avail = get_avail(&n_avail);
  int eventcode = PAPI_NULL, retval;
  PAPI_event_info_t info;

  for(i=0;i<n_avail;i++){
    if(get_avail == get_native_avail_papi_counters || get_avail == get_preset_avail_papi_counters){
      PAPI_event_name_to_code(avail[i],&eventcode);
      PAPI_get_event_info(eventcode, &info);
      printf("\t%s%*s, %s\n",info.symbol, 40-strlen(info.symbol)," ", info.long_descr);
      
      if(get_avail == get_native_avail_papi_counters &&
	 PAPI_enum_event(&eventcode, PAPI_NTV_ENUM_UMASKS) == PAPI_OK){
	printf("\t\t%s%*s, %s\n","Masks", 20-strlen("Masks")," ", "Mask description");
	do{
	  retval = PAPI_get_event_info(eventcode, &info);
	  if (retval == PAPI_OK){
	    if(parse_unit_masks(&info)){
	      printf("\t\t%s%*s, %s\n",info.symbol, 20-strlen(info.symbol)," ",info.long_descr);
	    }
	  }
	} while ( PAPI_enum_event( &eventcode, PAPI_NTV_ENUM_UMASKS) == PAPI_OK );
      }
    }
    else
      printf("%s\n",avail[i]);

    free(avail[i]);
  }
  free(avail);
}


int
check_papi_counter(char * counter_name)
{
  PAPI_event_info_t * info;
  int EventSet = PAPI_NULL;
  unsigned int event_code = 0x0;
  int err;

  if ((err=PAPI_create_eventset (&EventSet)) != PAPI_OK){
    fprintf(stderr,"Failed to create a PAPI eventset\n");
    exit(1);
  }
  if((err = PAPI_event_name_to_code(counter_name,&event_code)) != PAPI_OK){
    return err;
  }
  if((err = PAPI_add_event (EventSet, event_code)) != PAPI_OK) {
    return err;
  }

  PAPI_remove_named_event (EventSet, counter_name);
  if ( PAPI_destroy_eventset( &EventSet ) != PAPI_OK ){
    printf("**********  Call to destroy eventset failed when trying to validate event '%s'  **********\n", counter_name);
  }
  return err;
}

void
check_hwloc_obj_name(char * obj_name)
{
  hwloc_obj_type_t typep;
  int depthattrp;
  char typeattrp[sizeof(hwloc_obj_cache_type_t)];
  if(hwloc_obj_type_sscanf(obj_name, &typep, &depthattrp, typeattrp, sizeof(hwloc_obj_cache_type_t))==-1){
    fprintf(stderr,"Wrong hwloc_obj name: %s\n",obj_name);
    fprintf(stderr,"Available objs are:\n");
    dump_avail(get_avail_hwloc_objs_names);
    exit(1);
  }
}

void 
check_map_event_obj(hwloc_topology_t topology, char * obj_name, char * event_name)
{
  PAPI_event_info_t info;
  int err, eventcode;
  hwloc_obj_t obj;
  unsigned depth = hwloc_get_obj_depth_by_name(topology,obj_name);
  if((err = PAPI_event_name_to_code(event_name,&eventcode))!=PAPI_OK){
    handle_error(err);
    fprintf(stderr,"could not get \"%s\" event infos\n",event_name);
  }
  
  PAPI_get_event_info(eventcode,&info);
  switch(info.location){
  case PAPI_LOCATION_UNCORE:
      if(depth >= hwloc_get_type_depth(topology, HWLOC_OBJ_CORE)){
	fprintf(stderr,"event %s is an UNCORE event and is actually mapped on %s which is under HWLOC_OBJ_CORE\n",
		event_name, obj_name);
      }
      break;      
    case PAPI_LOCATION_CPU:
      if(depth > hwloc_get_type_depth(topology, HWLOC_OBJ_MACHINE)){
	fprintf(stderr,"event %s is a CPU event and is actually mapped on %s which is strictly under HWLOC_OBJ_MACHINE\n",
		event_name, obj_name);
      }
      break;
    case PAPI_LOCATION_PACKAGE:
      if(depth > hwloc_get_type_depth(topology, HWLOC_OBJ_SOCKET)){
	fprintf(stderr,"event %s is a PACKAGE event and is actually mapped on %s which is strictly under HWLOC_OBJ_SOCKET\n",
		event_name, obj_name);
      }
      break;
    default:
      break;
    }
}


