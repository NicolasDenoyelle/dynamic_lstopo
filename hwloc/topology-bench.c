/*
 * Copyright © 2012-2014 Inria.  All rights reserved.
 * See COPYING in top-level directory.
 */

#include <private/autogen/config.h>
#include <hwloc.h>
#include <libmbench.h>
#include <private/private.h>
#include <stdlib.h>

long long
hwloc_bench_memory_node(hwloc_obj_t node){
  return 10;
}

static int
hwloc_bench_memory_level(hwloc_obj_t level)
{
  hwloc_obj_t sibling = level;
  unsigned bandwidth;
  char bw_str[64];
  do{
    bandwidth = hwloc_bench_memory_node(sibling);
    memset(bw_str,0,64);
    sprintf(bw_str,"%uGB/s",bandwidth);
    hwloc_obj_add_info(sibling, "bandwidth", bw_str);
    sibling = sibling->next_cousin;
  } while(sibling != level && sibling != NULL);
  return 0;
}

static int
hwloc_bench_memory_type(struct hwloc_backend *backend, const hwloc_obj_type_t type){
  unsigned    depth = hwloc_topology_get_depth(backend->topology);
  hwloc_obj_t level = hwloc_get_obj_by_depth(backend->topology,--depth,0);
  char level_type[64];
  while(level!=NULL){
    level = hwloc_get_ancestor_obj_by_type(backend->topology,type, level);
    if(level!=NULL){
      hwloc_obj_type_snprintf(level_type,64,level,1); 
      //      printf("benchmark %s ...\n",level_type);
      hwloc_bench_memory_level(level);
    }
  }
  return 0;
}

static int
hwloc_bench_memory(struct hwloc_backend *backend)
{
  //  printf("Benchmark discovery starts...\n");
  hwloc_bench_memory_type(backend, HWLOC_OBJ_CACHE);
  hwloc_bench_memory_type(backend, HWLOC_OBJ_NODE);
  //  printf("Benchmark discovery ends\n");
  return 0;
}


static struct hwloc_backend *
hwloc_bench_component_instantiate(struct hwloc_disc_component *component __hwloc_attribute_unused,
				 const void *_data1 __hwloc_attribute_unused,
				 const void *_data2 __hwloc_attribute_unused,
				 const void *_data3 __hwloc_attribute_unused)
{
  struct hwloc_backend *backend; 
  /* thissystem may not be fully initialized yet, we'll check flags in discover() */
  backend = hwloc_backend_alloc(component);
  if (!backend)
    return NULL;
  backend->flags = HWLOC_BACKEND_FLAG_NEED_LEVELS;
  backend->discover = hwloc_bench_memory;
  return backend;
}

static struct hwloc_disc_component hwloc_bench_disc_component = {
  HWLOC_DISC_COMPONENT_TYPE_MISC,
  "bench",
  HWLOC_DISC_COMPONENT_TYPE_GLOBAL,
  hwloc_bench_component_instantiate,
  10,
  NULL
};

HWLOC_DECLSPEC extern const struct hwloc_component hwloc_bench_component; /* never linked statically in the core */

const struct hwloc_component hwloc_bench_component = {
  HWLOC_COMPONENT_ABI,
  NULL,
  NULL,
  HWLOC_COMPONENT_TYPE_DISC,
  0,
  &hwloc_bench_disc_component
};
