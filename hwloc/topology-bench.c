/*
 * Copyright © 2012-2014 Inria.  All rights reserved.
 * See COPYING in top-level directory.
 */

#include <private/autogen/config.h>
#include <hwloc.h>
#include <hwloc/linux.h>
#include <libmbench.h>
#include <bench.h>
#include <private/private.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

#define HWLOC_BENCH_MAX(a,b) (a>b?a:b)

#define BENCH_FUN_TYPE_STREAM 1
#define BENCH_FUN_TYPE_STREAM_STREAM 2

struct shared_thread_arg{
  hwloc_topology_t  topology;
  pthread_barrier_t br;
  pthread_mutex_t   mtx;
  uint64_t          size;
  uint64_t          align_size;
  double            bandwidth;
  perf_t            (*bench_fun)();
};

struct private_thread_arg{
  struct shared_thread_arg * sh_arg;
  hwloc_bitmap_t             cpuset;
};


static void *
bench_stream_thread(void * arg)
{
  if(arg==NULL)
    return NULL;
  struct private_thread_arg * parg = (struct private_thread_arg *) arg;
  hwloc_set_cpubind(parg->sh_arg->topology, parg->cpuset, HWLOC_CPUBIND_STRICT|HWLOC_CPUBIND_THREAD);
  struct timeval t_end, t_start;
  perf_t p;
  stream_t * s;

  s = mbench_stream_new (parg->sh_arg->size,parg->sh_arg->align_size);

  pthread_barrier_wait(&parg->sh_arg->br);
  gettimeofday(&t_start,NULL);
  p = parg->sh_arg->bench_fun(s);
  gettimeofday(&t_end, NULL);
  pthread_barrier_wait(&parg->sh_arg->br);

  pthread_mutex_lock(&parg->sh_arg->mtx);
  parg->sh_arg->bandwidth += 1000000 * (double) p.bytes / ((t_end.tv_sec * 1000000 + t_end.tv_usec) - (t_start.tv_sec * 1000000 + t_start.tv_usec));
  pthread_mutex_unlock(&parg->sh_arg->mtx);

  mbench_stream_free(s);
  return NULL;
}

static void *
bench_stream_stream_thread(void * arg)
{
  if(arg==NULL)
    return NULL;
  struct private_thread_arg * parg = (struct private_thread_arg *) arg;
  hwloc_set_cpubind(parg->sh_arg->topology, parg->cpuset, HWLOC_CPUBIND_STRICT|HWLOC_CPUBIND_THREAD);
  struct timeval t_end, t_start;
  perf_t p;
  stream_t * s1, *s2;

  s1 = mbench_stream_new (parg->sh_arg->size/2,parg->sh_arg->align_size);
  s2 = mbench_stream_new (parg->sh_arg->size/2,parg->sh_arg->align_size);

  pthread_barrier_wait(&parg->sh_arg->br);
  gettimeofday(&t_start,NULL);
  p = parg->sh_arg->bench_fun(s1,s2);
  gettimeofday(&t_end, NULL);
  pthread_barrier_wait(&parg->sh_arg->br);

  pthread_mutex_lock(&parg->sh_arg->mtx);
  parg->sh_arg->bandwidth += 1000000 * (double) p.bytes / ((t_end.tv_sec * 1000000 + t_end.tv_usec) - (t_start.tv_sec * 1000000 + t_start.tv_usec));
  pthread_mutex_unlock(&parg->sh_arg->mtx);

  mbench_stream_free(s1);
  mbench_stream_free(s2);
  return NULL;
}

static double
hwloc_bench_memory_node(hwloc_topology_t topology, hwloc_obj_t node, int bench_fun_type, perf_t (*fun)())
{  
  struct hwloc_cache_attr_s * attr = (struct hwloc_cache_attr_s *)node->attr;  
  unsigned i,n_threads = hwloc_bitmap_weight(node->cpuset);
  pthread_t threads[n_threads];
  struct private_thread_arg t_arg[n_threads];
  struct shared_thread_arg  sh_arg;
  hwloc_bitmap_t node_cpuset = hwloc_bitmap_dup(node->cpuset);

  sh_arg.bandwidth=0;
  sh_arg.topology=topology;
  sh_arg.bench_fun = fun;      
  if(node->memory.local_memory==0 || node->memory.page_types==NULL){
    if(attr->size==0 || attr->linesize == 0){
      char level_type[64];
      hwloc_obj_type_snprintf(level_type,64,node,1); 
      fprintf(stderr,"node %s is not a memory node and cannot be benchmarked.\n",level_type);
      return -1;
    }
    sh_arg.size = attr->size/n_threads;
    sh_arg.align_size = attr->linesize;
  }
  else{
    sh_arg.size = node->memory.local_memory/n_threads;
    sh_arg.align_size = node->memory.page_types[0].size;
  }

  pthread_barrier_init(&sh_arg.br,NULL,n_threads);
  pthread_mutex_init(&sh_arg.mtx,NULL);

  for(i=0;i<n_threads;i++){
    t_arg[i].sh_arg = &sh_arg;
    t_arg[i].cpuset = hwloc_bitmap_dup(node_cpuset);
    hwloc_bitmap_only(t_arg[i].cpuset,hwloc_bitmap_first(t_arg[i].cpuset));
    hwloc_bitmap_clr(node_cpuset, hwloc_bitmap_first(t_arg[i].cpuset));
    switch(bench_fun_type){
    case BENCH_FUN_TYPE_STREAM:
      pthread_create(&threads[i],NULL,bench_stream_thread,&t_arg[i]);
      break;
    case BENCH_FUN_TYPE_STREAM_STREAM:
      pthread_create(&threads[i],NULL,bench_stream_stream_thread,&t_arg[i]);
      break;
    default:
      return -1;
      break;
    }
  }
  for(i=0;i<n_threads;i++){
    pthread_join(threads[i],NULL);
    hwloc_bitmap_free(t_arg[i].cpuset);
  }

  pthread_barrier_destroy(&sh_arg.br);
  pthread_mutex_destroy(&sh_arg.mtx);
  hwloc_bitmap_free(node_cpuset);
  
  return sh_arg.bandwidth;
}

#define hwloc_bandwidth_to_str(str,bandwidth,KiB,MiB,GiB)		\
  memset(str,0,sizeof(str));						\
  if(bandwidth>=GiB)							\
    snprintf(str,sizeof(str),"%lldGiB/s",(long long)bandwidth/GiB);	\
  else if(bandwidth>=MiB)						\
    snprintf(str,sizeof(str),"%lldMiB/s",(long long)bandwidth/MiB);	\
 else if(bandwidth>=KiB)						\
   snprintf(str,sizeof(str),"%lldKiB/s",(long long)bandwidth/KiB);	\
 else									\
   snprintf(str,sizeof(str),"%lldiB/s",(long long)bandwidth);		


struct bench_info{
  double bandwidth;
  char bandwidth_str[64];
};

static int 
benchmark_and_get_info(hwloc_topology_t topology, hwloc_obj_t level, 
		       int bench_type, 
		       perf_t (*fun)(), 
		       const char * bench_str, 
		       struct bench_info * infos,
		       const long long KiB, const long long MiB, const long long GiB,
		       double * max_bandwidth)
{
  infos->bandwidth  = hwloc_bench_memory_node(topology, level, bench_type, fun);
  if(infos->bandwidth == -1)
    return 1;
  hwloc_bandwidth_to_str(infos->bandwidth_str, infos->bandwidth ,KiB,MiB,GiB);
  *max_bandwidth = HWLOC_BENCH_MAX(*max_bandwidth,infos->bandwidth);  
  printf("\t%s bandwidth = %s\n", bench_str, infos->bandwidth_str);   
  return 0;
}

static int
hwloc_bench_memory_level(hwloc_topology_t topology, hwloc_obj_t level)
{

  struct hwloc_cache_attr_s * attr = (struct hwloc_cache_attr_s *)level->attr;  
  if(attr->type == HWLOC_OBJ_CACHE_INSTRUCTION){
    return -1;
  }

  const long long KiB = pow(2,10);
  const long long MiB = pow(KiB,2);
  const long long GiB = pow(KiB,3);

  double max_bandwidth = 0;
  char   max_bandwidth_str[64];

  struct bench_info load, store, copy, ls, ll;

  char level_type[64];
  hwloc_obj_type_snprintf(level_type,64,level,1); 
  printf("benchmark %s ...\n",level_type);

  if(benchmark_and_get_info(topology, level, 1, mbench_load, "load", &load, KiB, MiB, GiB, &max_bandwidth) == 1)
    return 1;

  if(benchmark_and_get_info(topology, level, 1, mbench_store, "store", &store, KiB, MiB, GiB, &max_bandwidth) == 1)
    return 1;

  if(benchmark_and_get_info(topology, level, 2, mbench_copy, "copy", &copy, KiB, MiB, GiB, &max_bandwidth) == 1)
    return 1;

  if(benchmark_and_get_info(topology, level, 2, mbench_ll, "ll", &ll, KiB, MiB, GiB, &max_bandwidth) == 1)
    return 1;

  hwloc_bandwidth_to_str(max_bandwidth_str, max_bandwidth ,KiB,MiB,GiB);
  hwloc_obj_t sibling = level;
  do{
    hwloc_obj_add_info(sibling, "bandwidth_load" , load.bandwidth_str);
    hwloc_obj_add_info(sibling, "bandwidth_store", store.bandwidth_str);
    hwloc_obj_add_info(sibling, "bandwidth_copy" , copy.bandwidth_str);
    hwloc_obj_add_info(sibling, "bandwidth_ll"   , ll.bandwidth_str);
    hwloc_obj_add_info(sibling, "bandwidth"      , max_bandwidth_str);
    
    sibling = sibling->next_cousin;
  } while(sibling != level && sibling != NULL);
  return 0;
}

static int
hwloc_bench_memory_type(struct hwloc_backend *backend, const hwloc_obj_type_t type){
  unsigned    depth = hwloc_topology_get_depth(backend->topology);
  hwloc_obj_t level = hwloc_get_obj_by_depth(backend->topology,--depth,0);
  while(level!=NULL){
    level = hwloc_get_ancestor_obj_by_type(backend->topology,type, level);
    if(level!=NULL){
      hwloc_bench_memory_level(backend->topology, level);
    }
  }
  return 0;
}

static int
hwloc_bench_memory(struct hwloc_backend *backend)
{
  hwloc_debug("Benchmark discovery starts...\n");
  hwloc_bench_memory_type(backend, HWLOC_OBJ_CACHE);
  hwloc_bench_memory_type(backend, HWLOC_OBJ_NODE);
  hwloc_debug("Benchmark discovery ends\n");
  return 0;
}


static struct hwloc_backend *
hwloc_bench_component_instantiate(struct hwloc_disc_component *component __hwloc_attribute_unused,
				 const void *_data1 __hwloc_attribute_unused,
				 const void *_data2 __hwloc_attribute_unused,
				 const void *_data3 __hwloc_attribute_unused)
{
  char * env = getenv("HWLOC_BENCH");
  if(env==NULL || !strcmp(env,"") || !strcmp(env,"0"))
    return NULL;

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
