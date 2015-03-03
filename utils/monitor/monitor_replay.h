#ifndef REPLAY_H
#define REPLAY_H

#include "monitor_utils.h"
#include <semaphore.h>

#define BUF_MAX 128

struct replay_queue{
  double val;
  struct replay_queue * next;
  struct replay_queue * prev;
};

struct replay_node{
  struct replay_queue * head;
  struct replay_queue * tail;
  unsigned count;
  pthread_mutex_t mtx;  
};

struct replay_node * new_replay_node();
void                 delete_replay_node(struct replay_node * node);
int                  replay_node_insert_value(struct replay_node * out, double in);
double               replay_node_get_value(struct replay_node * rn);

struct replay_t{
  /**** private ****/
  FILE * input;
  int    eof;
  sem_t buffer_semaphore; /* increment each time a timestamp is enqueued in timestamps */
  int phase;

  long long trace_start; /* the younger real_usec read from trace */
  struct replay_node * timestamps; /* The first sample's time stamp of each topology's set of samples */
  struct value_line last_read; /* if a node buffer was full when attempting to enqueue a value, we store read content here */
  int last_read_read; /* 0 if the last_read has not be read yet, 1 if it has*/
  pthread_t fill_thread, timer_thread;
  long long usleep_len;
  pthread_mutex_t mtx;

  unsigned count; /* number of monitor levels */
  unsigned  * visited;/* topology_depth */  
  unsigned  * depths;
  double    * max, * min; /* [n_monitors] */  
  unsigned n_nodes;
  hwloc_obj_t * nodes;
  unsigned nodes_filled;

  /**** public ****/
  hwloc_topology_t topology; /* stores a replay_node in each obj sibling at depth "depths[i]" which first element is the value to be read */
  int update_read_fd;        /* should block until one reader is allowed to read values */
  int update_write_fd;        /* should block until one reader is allowed to read values */
};
typedef struct replay_t * replay_t;

replay_t new_replay      (const char * filename, hwloc_topology_t topology, int phase);
void     delete_replay   (replay_t r);
void     replay_start    (replay_t r);
int      replay_is_finished(replay_t r);

#endif
