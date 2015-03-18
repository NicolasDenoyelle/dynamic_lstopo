#ifndef REPLAY_H
#define REPLAY_H

#include "monitor_utils.h"
#include <semaphore.h>

#define BUF_MAX 128

struct replay_queue{
  pthread_mutex_t mtx;
  struct replay_queue_element * head;
  struct replay_queue_element * tail;
};

struct replay_queue_element{
  struct value_line vl;
  struct replay_queue_element * next;
  struct replay_queue_element * prev;
};

struct replay_t{
  hwloc_topology_t topology;
  FILE * input;
  int    eof;
  sem_t buffer_semaphore; /* increment each time a timestamp is enqueued in timestamps */
  int phase;
  float speed;

  long long trace_start; /* the younger real_usec read from trace */
  struct value_line last_read; /* if a node buffer was full when attempting to enqueue a value, we store read content here */
  int last_read_read; /* 0 if the last_read has not be read yet, 1 if it has*/
  pthread_t fill_thread, timer_thread;
  long long usleep_len;
  pthread_mutex_t mtx;

  double    * max, * min; /* topo_depth */    

  hwloc_obj_t * nodes;  /* index correspond to the value_line.id. userdata contain double[3] history of values*/
  struct replay_queue * val_queue, * timestamp_queue;
  int update_read_fd;   /* should block until one reader is allowed to read values */
  int update_write_fd;  /* should block until one reader is allowed to read values */
};
typedef struct replay_t * replay_t;

replay_t new_replay        (const char * filename, hwloc_topology_t topology, int phase, float speed);
void     delete_replay     (replay_t r);
void     replay_start      (replay_t r);
int      replay_is_finished(replay_t r);
int      replay_get_value  (replay_t r, struct value_line * out);

#endif
