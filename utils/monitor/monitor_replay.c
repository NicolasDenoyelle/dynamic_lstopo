#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <float.h>
#include "hwloc.h"
#include "monitor_replay.h"
#include "monitor_utils.h"

#define M_alloc(pointer,n_elem,size)	\
  if((pointer=calloc(n_elem,size))==NULL){		\
    perror("calloc failed");				\
    exit(EXIT_FAILURE);					\
  }							

void output_header(int output_fd){
  dprintf(output_fd,"#-----+---------+-------------------+-------------------+-------------------+\n");
  dprintf(output_fd,"%6s", "idx");
  dprintf(output_fd,"%10s","level");
  dprintf(output_fd,"%20s","real_usec");
  dprintf(output_fd,"%20s","monitor_name");
  dprintf(output_fd,"%20s","monitor_value");
  dprintf(output_fd,"\n");
  dprintf(output_fd,"#-----+---------+-------------------+-------------------+-------------------+\n");
}

void
output_line_content(int output_fd, struct line_content * in){
  dprintf(output_fd,"%6d",in->sibling_idx);
  dprintf(output_fd,"%10s",in->obj_name);
  dprintf(output_fd,"%20lld",in->real_usec);
  dprintf(output_fd,"%20s",in->name);
  dprintf(output_fd,"%20lf\n",in->value);
}

ssize_t
input_line_content(FILE * in, struct line_content * out){
  ssize_t ret;
  size_t len = 0;
  char * line = NULL;
  if((ret = getline(&line,&len,in))<0){
    perror("getline");
    return ret;
  }

  char * name, * obj_name;
  int n;
  errno = 0;
  n = sscanf(line,"%6d%10s%20lld%20s%20lf",&(out->sibling_idx),out->obj_name,&(out->real_usec), out->name, &(out->value));
  free(line);
  if (errno != 0) {
    perror("scanf");
  } else if(n!=5)
    return input_line_content(in, out);
  else{
    strcpy(out->name,name);
    strcpy(out->obj_name,obj_name);
    free(obj_name);
    free(name);
  }
  return ret;
}

inline void 
chk_update_max(hwloc_topology_t topology, unsigned depth, double max){
  hwloc_obj_t obj = hwloc_get_obj_by_depth(topology,depth,0);
  if(((struct replay_node *)obj->userdata)->max < max){
    do{
      ((struct replay_node *)obj->userdata)->max = max;
    } while((obj = obj->next_sibling) != NULL && obj->logical_index!=0);
  }
}

inline void 
chk_update_min(hwloc_topology_t topology, unsigned depth, double min){
  hwloc_obj_t obj = hwloc_get_obj_by_depth(topology,depth,0);
  if(((struct replay_node *)obj->userdata)->min > min){
    do{
      ((struct replay_node *)obj->userdata)->min = min;
    } while((obj = obj->next_sibling) != NULL && obj->logical_index!=0);
  }
}

struct replay_node * 
new_replay_node(){
struct replay_node * node;
  M_alloc(node,1,sizeof(struct replay_node));
  node->max = DBL_MIN;
  node->min = DBL_MAX;
  M_alloc(node->head,1,sizeof(struct replay_queue));
  node->tail = NULL;
  struct replay_queue * current=node->head;
  current->prev=NULL;
  unsigned i;
  for(i=0;i<BUF_MAX-1;i++){
    current->val=0;
    M_alloc(current->next,1,sizeof(struct replay_queue));
    current->next->prev = current;
    current = current->next;
  }
  current->val=0;
  current->next=node->head;
  node->head->prev=current;
  return node;
}

void
delete_replay_node(struct replay_node * node)
{
  struct replay_queue *next, * current = node->head->next;
  while(current!=node->head){
    next = current->next;
    free(current);
    current = next;
  } 
  free(node->head);
  free(node);
}

int
replay_node_insert_value(struct replay_node * out, double in){
  /* end of circular queue */
  if(out->tail==out->head)
    return -1;
  /* first element to be inserted */
  if(out->tail==NULL){
    out->head->val = in;
    out->tail = out->head->next;
  }
  else{
    out->tail->val=in;
    out->tail = out->tail->next;
  }
}

double
replay_node_remove_value(struct replay_node * rn){
  /* empty_queue */
  if(rn->tail==NULL)
    return 0;
  double val = rn->head->val;
  rn->head = rn->head->next;
  /* the queue is empty */
  if(rn->head==rn->tail)
    rn->tail==NULL;
  return val;
}

inline double
replay_node_get_value(struct replay_node * rn)
{
  return replay_node_remove_value(rn);
}

inline double 
replay_node_get_max_value(struct replay_node * node)
{
  return node->max;
}

inline double
replay_node_get_min_value(struct replay_node * node)
{
  return node->min;
}

int replay_input_line(replay_t r){
  struct line_content lc;
  int read;
  if(r->last_read_read==0)
    lc = r->last_read;
  else
    read = input_line_content(r->input,&lc);
  /*error, end of file, or nothing read */
  if(read<=0)
    return read;
  
  unsigned depth = hwloc_get_obj_depth_by_name(r->topology, lc.obj_name);
  chk_update_min(r->topology,depth,lc.value);
  chk_update_max(r->topology,depth,lc.value);
  struct replay_node * node = (struct replay_node *)(hwloc_get_obj_by_depth(r->topology,depth,lc.sibling_idx)->userdata);
  /* if we cannot insert the value, we save the content read to insert it next time */
  if(replay_node_insert_value(node, lc.value)==-1){
    strncpy(r->last_read.obj_name,lc.obj_name,10);
    strncpy(r->last_read.name,lc.name,20);
    r->last_read.value = lc.value;
    r->last_read.sibling_idx = lc.sibling_idx;
    r->last_read.real_usec = lc.real_usec;
    r->last_read_read=0;
    return 1;
  }
  else{ /* insert successed we also insert timestamp */
    if(r->n_read==0){
      /* this one should never fail if precedent didn't fail */
      replay_node_insert_value(r->timestamps,lc.real_usec);
      sem_post(&r->buffer_semaphore);
    }
    r->n_read = (1+r->n_read)%r->n_nodes;
  }
  return 2;
}

replay_t
new_replay(const char * filename, hwloc_topology_t topology)
{
  FILE * input = fopen(filename,"r");
  if(input==NULL){
    perror("fopen");
    exit(EXIT_FAILURE);
  }

  replay_t rp;
  M_alloc(rp,1,sizeof(struct replay_t));
  rp->input = input;
  rp->n_nodes=0;  
  rp->n_read=0;  
  rp->last_read_read=1;
  rp->eof=0;
  rp->timestamps = new_replay_node();

  if(topology==NULL)
    topology_init(&rp->topology);
  else
    hwloc_topology_dup(&rp->topology,topology);

  if(sem_init(&rp->reader_semaphore, 0, 0)){
    perror("sem_init");
    exit(EXIT_FAILURE);
  }
  
  if(sem_init(&rp->buffer_semaphore, 0, 0)){
    perror("sem_init");
    exit(EXIT_FAILURE);
  }

  if(pthread_mutex_init(&rp->pause_mtx, NULL)){
    perror("pthread_mutex_init");
    exit(EXIT_FAILURE);
  }

  int depth, err=0, i=0;
  unsigned topo_depth = hwloc_topology_get_depth(rp->topology);
  M_alloc(rp->depths,topo_depth,sizeof(unsigned));
  for(i=0;i<topo_depth;i++){
    rp->depths[i]=0;
  }

  hwloc_obj_t obj = hwloc_get_root_obj (rp->topology);
  struct line_content lc;

  
  if((err = input_line_content(rp->input,&lc))==-1){
    fprintf(stderr,"nothing to read in %s\n",filename);
    exit(EXIT_SUCCESS);
  }
  rp->trace_start = lc.real_usec;
  depth = hwloc_get_obj_depth_by_name(rp->topology, lc.obj_name);
  obj = hwloc_get_obj_by_depth(rp->topology,depth,lc.sibling_idx);
    
  /* allocate topology buffers */
  while(obj->userdata==NULL && err != -1 && i<topo_depth){
    if(lc.sibling_idx==0)
      rp->depths[i++] = depth;
    if(obj==NULL){
      fprintf(stderr,"input file:%s contains object %s at depth %d and index %d which doesn't exists in current machine topology\n",filename,lc.obj_name,depth,lc.sibling_idx);
      exit(EXIT_FAILURE);
    }
    obj->userdata = new_replay_node();
    if((err = input_line_content(rp->input,&lc))==-1)
      break;
    depth = hwloc_get_obj_depth_by_name(rp->topology, lc.obj_name);
    obj = hwloc_get_obj_by_depth(rp->topology,depth,lc.sibling_idx);
    rp->n_nodes++;
  } 
  rp->count=i;
  rewind(rp->input);

  /* fill topology_buffers */
  err=0;  
  do{
    err = replay_input_line(rp);
  } while(err > 1);
  
  /* filling stopped beacause buffers were full then we can have an approximation of the time interval between 
     two sets of sample */
  if(err==1){
    rp->sample_interval = rp->last_read.real_usec - rp->timestamps->head->val;
  }
  else{
    rp->eof=1;
    rp->sample_interval = 0;
  }
  return rp;
}

void
delete_replay(replay_t r)
{
  hwloc_obj_t obj;
  unsigned i;
  pthread_cancel(r->fill_thread);
  pthread_cancel(r->timer_thread);
  pthread_join(r->fill_thread,NULL);
  pthread_join(r->timer_thread,NULL);
  sem_destroy(&r->reader_semaphore);
  sem_destroy(&r->buffer_semaphore);
  pthread_mutex_destroy(&r->pause_mtx);
  delete_replay_node(r->timestamps);
  for(i=0;i<r->count;i++){
    obj = hwloc_get_obj_by_depth(r->topology,r->depths[i],0);
    do{
      delete_replay_node((struct replay_node *)obj->userdata);
    } while((obj=obj->next_sibling)!=NULL && obj->userdata!=NULL);
  }
  free(r->depths);
  fclose(r->input);
  hwloc_topology_destroy(r->topology);
  free(r);
}


void * replay_fill_thread(void* arg){
  replay_t r = (replay_t)arg;
  long long usleep_len = r->sample_interval*BUF_MAX/2;
  int err;
  while((err = replay_input_line(r)) > 0){
    /* buffers full */
    if(err==1){
      usleep(usleep_len);
    }
  }
  r->eof=1;
}

int
timeval_subtract (struct timeval * result, struct timeval * x, struct timeval * y)
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}


int
replay_is_finished(replay_t r){
  unsigned sem_count;
  sem_getvalue(&r->buffer_semaphore,&sem_count);
  if(r->eof==0 || sem_count)
    return 0;
  else 
    return 1;
}

void * replay_timer_thread(void* arg){
  replay_t r = (replay_t)arg;
  struct timeval now, elapsed, pause;
  long long trace_elapsed, usleep_len;
  gettimeofday(&r->start,NULL);
  
  while(!replay_is_finished(r)){
    sem_wait(&r->buffer_semaphore);
    trace_elapsed = replay_node_get_value(r->timestamps) - r->trace_start;
    gettimeofday(&now,NULL);
    timeval_subtract(&elapsed, &now, &r->start);
    usleep_len = trace_elapsed - elapsed.tv_usec;
    if(usleep_len>0){
      usleep(usleep_len);
    }

    /* pause handling */
    pthread_mutex_lock(&r->pause_mtx);
    pthread_mutex_unlock(&r->pause_mtx);
    gettimeofday(&pause,NULL);
    timeval_subtract(&elapsed, &pause, &now);
    r->start.tv_sec+=elapsed.tv_sec;
    r->start.tv_usec+=elapsed.tv_usec;

    sem_post(&r->reader_semaphore);
  }
}

inline void replay_wait_read(replay_t r){
  sem_wait(&r->reader_semaphore);
}

void replay_start(replay_t r){
  pthread_create(&r->fill_thread,NULL,replay_fill_thread,r);
  pthread_create(&r->timer_thread,NULL,replay_timer_thread,r);
}

inline void
replay_pause(replay_t r)
{
  pthread_mutex_lock(&r->pause_mtx);
}

inline void
replay_resume(replay_t r)
{
  pthread_mutex_unlock(&r->pause_mtx);
}


