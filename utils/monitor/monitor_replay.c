#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/time.h>
#include <float.h>
#include <unistd.h>
#include "hwloc.h"
#include "monitor_replay.h"
#include "monitor_utils.h"

#define M_alloc(pointer,n_elem,size)	\
  if((pointer=calloc(n_elem,size))==NULL){		\
    perror("calloc failed");				\
    exit(EXIT_FAILURE);					\
  }							

void output_header(int output_fd){
  const char * sep = "-------------------+";
  dprintf(output_fd,"%6s%10s%20s%20s%20s\n","#----+","---------+",sep,sep,sep);
  dprintf(output_fd,"%6s%10s%20s%20s%20s\n","# idx ","level ","real_usec ","monitor_name ","monitor_value ");
  dprintf(output_fd,"%6s%10s%20s%20s%20s\n","#----+","---------+",sep,sep,sep);
}

inline void
output_line_content(int output_fd, struct line_content * in){
  dprintf(output_fd,"%6d%10s%20lld%20s%20lf\n",
	  in->sibling_idx, in->obj_name, in->real_usec, in->name, in->value);
}

ssize_t
input_line_content(FILE * in, struct line_content * out){
  ssize_t ret;
  char line[78];
  memset(line,0,78);
  if((ret = fread(line,1,77,in))<77){
    if(ret != 0){
      printf("read fail : %s\n",line);
    }
    return -1;
  }

  char sibling_idx[6], obj_name[10], real_usec[20], name[20], value[20];
  int n;
  n = sscanf(line,"%6s%10s%20s%20s%20s",sibling_idx,obj_name, real_usec, name, value);
  if (errno != 0) {
    perror("scanf");
  } 
  else if(n!=5 || sibling_idx[0] == '#'){
    return input_line_content(in, out);
  }
  else{
    strncpy(out->name,name,20);
    strncpy(out->obj_name,obj_name,10);
    out->real_usec=atoll(real_usec);
    out->sibling_idx=atoi(sibling_idx);
    out->value=atof(value);
  }
  return n;
}

static inline void 
chk_update_max(hwloc_topology_t topology, unsigned depth, double max){
  hwloc_obj_t obj = hwloc_get_obj_by_depth(topology,depth,0);
  if(obj->userdata && ((struct replay_node *)obj->userdata)->max < max){
    do{
      ((struct replay_node *)obj->userdata)->max = max;
    } while((obj = obj->next_sibling) != NULL && obj->logical_index!=0);
  }
}

static inline void 
chk_update_min(hwloc_topology_t topology, unsigned depth, double min){
  hwloc_obj_t obj = hwloc_get_obj_by_depth(topology,depth,0);
  if(obj->userdata && ((struct replay_node *)obj->userdata)->min > min){
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
  struct replay_queue * current=node->head;
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
  node->tail = NULL;
  pthread_mutex_init(&node->mtx,NULL);
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
  pthread_mutex_destroy(&node->mtx);
  free(node);
}

int
replay_node_insert_value(struct replay_node * out, double in){
  pthread_mutex_lock(&out->mtx);
  /* end of circular queue */
  if(out->tail==out->head){
    pthread_mutex_unlock(&out->mtx);
    return -1;
  }
  /* first element to be inserted */
  if(out->tail==NULL){
    out->head->val = in;
    out->tail = out->head->next;
  }
  else{
    out->tail->val=in;
    out->tail = out->tail->next;
  }
  pthread_mutex_unlock(&out->mtx);
  return 0;
}

double
replay_node_remove_value(struct replay_node * rn){
  pthread_mutex_lock(&rn->mtx);
  /* empty_queue */
  if(rn->tail==NULL){
    pthread_mutex_unlock(&rn->mtx);
    return 0;
  }
  double val = rn->head->val;
  rn->head = rn->head->next;
  /* the queue is empty */
  if(rn->head==rn->tail)
    rn->tail=NULL;
  pthread_mutex_unlock(&rn->mtx);
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
  if(r->last_read_read==0){
    lc = r->last_read;
    r->last_read_read=1;
  }
  else{
    read = input_line_content(r->input,&lc);
    /*error, end of file, or nothing read */
    if(read == -1){
      return read;
    }
  }
  

  unsigned depth = hwloc_get_obj_depth_by_name(r->topology, lc.obj_name);
  hwloc_obj_t obj = hwloc_get_obj_by_depth(r->topology,depth,lc.sibling_idx);
  if(obj->userdata==NULL){
    obj->userdata = new_replay_node();
    if(r->visited[depth]==0){
      r->depths[r->count++] = depth;
      r->visited[depth]=1;
    }
    r->n_nodes++;
  }
    
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
    r->nodes_filled++;
    if(r->nodes_filled%r->n_nodes == 0){
      /* this one should never fail if precedent didn't fail */
      replay_node_insert_value(r->timestamps,lc.real_usec);
      sem_post(&r->buffer_semaphore);
    }
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
  rp->last_read_read=1;
  rp->eof=0;
  rp->n_nodes=0;
  rp->nodes_filled=0;
  rp->timestamps = new_replay_node();

  if(topology==NULL)
    topology_init(&rp->topology);
  else
    hwloc_topology_dup(&rp->topology,topology);

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
  M_alloc(rp->visited,topo_depth,sizeof(unsigned));
  for(i=0;i<topo_depth;i++){
    rp->depths[i]=0;
    rp->visited[i]=0;
  }
  i=0;
  hwloc_obj_t obj = hwloc_get_root_obj (rp->topology);
  struct line_content lc;

  if((err = input_line_content(rp->input,&lc))==-1){
    fprintf(stderr,"nothing to read in %s\n",filename);
    exit(EXIT_SUCCESS);
  }
  rp->trace_start = lc.real_usec;
  rewind(input);
  /* fill topology_buffers */
  do{
    err = replay_input_line(rp);
  } while(err == 2);
  
  /* filling stopped because buffers were full then we can have an approximation of the time interval between 
     two sets of sample */
  if(err==1){
    rp->sample_interval = rp->timestamps->head->next->val - rp->timestamps->head->val;
  }
  else{
    rp->eof=1;
    rp->sample_interval = 0;
  }


  /* set fd for updates */
  int update_fds[2];
  if(pipe(update_fds)==-1){
    perror("pipe");
    exit(EXIT_FAILURE);
  }
  rp->update_read_fd = update_fds[0];
  rp->update_write_fd = update_fds[1];

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
  sem_destroy(&r->buffer_semaphore);
  pthread_mutex_destroy(&r->pause_mtx);
  delete_replay_node(r->timestamps);
  for(i=0;i<r->count;i++){
    obj = hwloc_get_obj_by_depth(r->topology,r->depths[i],0);
    do{
      if(obj->userdata){
	delete_replay_node((struct replay_node *)obj->userdata);
      }
    } while((obj=obj->next_sibling)!=NULL && obj->userdata!=NULL);
  }
  free(r->depths);
  free(r->visited);
  fclose(r->input);
  hwloc_topology_destroy(r->topology);
  free(r);
}


void * replay_fill_thread(void* arg){
  replay_t r = (replay_t)arg;
  long long usleep_len = r->sample_interval * BUF_MAX / 2;
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
  long long trace_elapsed, elapsed, diff;
  struct timeval start, now, wait;

  gettimeofday(&start,NULL);
  while(!replay_is_finished(r)){
    sem_wait(&r->buffer_semaphore);
    trace_elapsed = replay_node_get_value(r->timestamps) - r->trace_start;
    gettimeofday(&now,NULL);
    elapsed = 1000000*(now.tv_sec - start.tv_sec) + now.tv_usec -start.tv_usec;
    if((diff = trace_elapsed - elapsed) > 0){
      usleep(diff);
    }
    write(r->update_write_fd,"1",sizeof(uint64_t));
    
    /* pause handling */
    /* pthread_mutex_lock(&r->pause_mtx); */
    /* pthread_mutex_unlock(&r->pause_mtx); */
    /* gettimeofday(&pause,NULL); */
    /* timeval_subtract(&pause, &pause, &now); */
    /* start.tv_sec+=pause.tv_sec; */
    /* start.tv_usec+=pause.tv_usec; */

  }
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


