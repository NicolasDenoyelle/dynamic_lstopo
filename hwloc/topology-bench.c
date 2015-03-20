/*
 * Copyright © 2012-2014 Inria.  All rights reserved.
 * See COPYING in top-level directory.
 */

#include <private/autogen/config.h>
#include <hwloc.h>
#include <libmbench.h>
#include <private/private.h>

#include <stdlib.h>


static int
hwloc_bench_memory_node(hwloc_obj_t obj)
{
  return 0;
}

static int
hwloc_bench_new_obj(struct hwloc_backend *backend, struct hwloc_backend *caller, struct hwloc_obj *obj)
{
  printf("Benchmark notify new object.\n");
  return 0;
}

static int
hwloc_bench(struct hwloc_backend *backend)
{
  printf("Benchmark discovery starts...\n");
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
  backend->discover = hwloc_bench;
  backend->notify_new_object = hwloc_bench_new_obj;
  return backend;
}

static int
hwloc_bench_component_init(unsigned long flags)
{
  if (flags)
    return -1;
  if (hwloc_plugin_check_namespace("bench", "hwloc_backend_alloc") < 0)
    return -1;
  printf("bench component initialized\n");
  return 0;
}

static void
hwloc_bench_component_finalize(unsigned long flags)
{
  if (flags)
    return;
    printf("bench component finalized\n");
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
  hwloc_bench_component_init, 
  hwloc_bench_component_finalize,
  HWLOC_COMPONENT_TYPE_DISC,
  0,
  &hwloc_bench_disc_component
};
