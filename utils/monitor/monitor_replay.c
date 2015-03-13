#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/time.h>
#include <float.h>
#include <unistd.h>
#include <search.h>
#include <stdint.h>
#include "hwloc.h"
#include "monitor_replay.h"
#include "monitor_utils.h"

#define M_alloc(pointer,n_elem,size)	\
  if((pointer=calloc(n_elem,size))==NULL){		\
    perror("calloc failed");				\
    exit(EXIT_FAILURE);					\
  }							

void 
output_header_paje(monitors_t m)
{
  unsigned i,j;
  hwloc_obj_t obj;
  dprintf(m->output_fd,"%%EventDef Node_val 0\n");
  dprintf(m->output_fd,"%%   Id                int\n");
  dprintf(m->output_fd,"%%   Phase             int\n");
  dprintf(m->output_fd,"%%   Time_us           date\n");
  dprintf(m->output_fd,"%%   Value             double\n");
  dprintf(m->output_fd,"%%EndEventDef\n\n");

  dprintf(m->output_fd,"%%EventDef Node_container 1\n");
  dprintf(m->output_fd,"%%   Id                int\n");
  dprintf(m->output_fd,"%%   Level             string\n");
  dprintf(m->output_fd,"%%   Sibling           int\n");
  dprintf(m->output_fd,"%%   Name              string\n");
  dprintf(m->output_fd,"%%EndEventDef\n\n");

  for(i=0;i<m->count;i++){
    for(j=0;j<hwloc_get_nbobjs_by_depth(m->topology,m->depths[i]);j++){
      obj = hwloc_get_obj_by_depth(m->topology,m->depths[i],j);
      dprintf(m->output_fd,"1 %d %s %d %s\n",((struct monitor_node*)obj->userdata)->id, m->depth_names[i], j, m->names[i]);
    }
  }

  dprintf(m->output_fd,"\n");
}

inline void
output_line_content_paje(int output_fd, struct value_line * in){
  dprintf(output_fd,"0 %d %d %lld %lf\n",in->id, in->phase, in->real_usec, in->value);
}


#define HL 1
#define VL 2
union input_line{
  struct header_line hl;
  struct value_line vl;
};

int replay_input_paje_line(replay_t r, union input_line * line){
  char *read = NULL;
  ssize_t len, err;

  if(r->last_read_read==0){
    line->vl = r->last_read;
    r->last_read_read=1;
    return VL;
  }
  else{
    err = getline(&read,&len,r->input);
    if(err==-1){
      return err;
    }
    if(read[0]=='1'){
      if(line!=NULL)
	sscanf(read,"%*d %d %10s %d %*20s",&(line->hl.id),line->hl.level,&(line->hl.sibling));
      free(read);
      return HL;
    }
    if(read[0]=='0'){
      if(line!=NULL)
	sscanf(read,"%*d %d %u %lld %lf",&(line->vl.id),&(line->vl.phase),&(line->vl.real_usec),&(line->vl.value));
      free(read);
      return VL;
    }
    else {
      free(read);
      return replay_input_paje_line(r,line);
    }
  }    
}

struct replay_queue_element *
new_replay_queue_element(struct replay_queue_element * prev, struct replay_queue_element * next)
{
  struct replay_queue_element * elm;
  M_alloc(elm,1,sizeof(struct replay_queue_element));
  elm->next = next;
  elm->prev = prev;
  elm->vl.id=0;
  elm->vl.phase=0;
  elm->vl.real_usec=0;
  elm->vl.value=0;
  return elm;
}

struct replay_queue * 
new_replay_queue(){
  struct replay_queue * queue;
  struct replay_queue_element *current;
  unsigned i;

  M_alloc(queue,1,sizeof(struct replay_queue));
  queue->tail=NULL;
  queue->head=new_replay_queue_element(NULL,NULL);
  current = queue->head;
  for(i=1;i<BUF_MAX-1;i++){
    current = new_replay_queue_element(current,NULL);
    current->prev->next=current;
  }
  current->next = queue->head;
  pthread_mutex_init(&queue->mtx,NULL);
  return queue;
}

void
delete_replay_queue(struct replay_queue * queue)
{
  struct replay_queue_element * tmp = queue->head;
  if(queue->tail==NULL){
    free(tmp);
    goto end;
  }
  
  while(tmp!=queue->tail->prev){
    queue->head = queue->head->next;
    free(tmp);
    tmp = queue->head;
  }
  free(tmp);

 end:
  pthread_mutex_destroy(&queue->mtx);
  free(queue);
}

int
replay_queue_push(struct replay_queue * queue, struct value_line in){
  pthread_mutex_lock(&queue->mtx);
  /* end of circular queue */
  if(queue->tail==queue->head){
    pthread_mutex_unlock(&queue->mtx);
    return -1;
  }
  /* first element to be inserted */
  if(queue->tail==NULL){
    queue->head->vl = in;
    queue->tail = queue->head->next;
  }
  else{
    queue->tail->vl=in;
    queue->tail = queue->tail->next;
  }
  pthread_mutex_unlock(&queue->mtx);
  return 0;
}

int
replay_queue_pop(struct replay_queue * queue, struct value_line * out){
  pthread_mutex_lock(&queue->mtx);
  /* empty_queue */
  if(queue->tail==NULL){
    pthread_mutex_unlock(&queue->mtx);
    //    printf("read value 0\n");
    return 0;
  }
  *out = queue->head->vl;
  queue->head = queue->head->next;
  /* the queue is empty */
  if(queue->head==queue->tail)
    queue->tail=NULL;
  pthread_mutex_unlock(&queue->mtx);
  //  printf("read value %lf\n",val);
  return 1;
}

int
replay_input_line(replay_t r){
  unsigned depth;
  hwloc_obj_t obj;
  int line_type, i,j;
  union input_line il;
  line_type = replay_input_paje_line(r,&il);
  switch(line_type){
    /*end of file*/
  case -1:
    return -1;
    break;
    /*header line*/
  case HL:
    return replay_input_line(r);
    break;
    /*value line*/
  case VL:
    if(il.vl.phase!=r->phase){
      return replay_input_line(r);
      break;
    }
    obj = r->nodes[il.vl.id];
    /* if we cannot insert the value, we save the content read to insert it next time */
    if(replay_queue_push(r->timestamp_queue,il.vl)==-1){
      r->last_read.value = il.vl.value;
      r->last_read.phase = il.vl.phase;
      r->last_read.real_usec = il.vl.real_usec;
      r->last_read_read=0;
      return 0;
      break;
    }
    else
      sem_post(&r->buffer_semaphore);
    return VL;
    break;
    /*never happens*/
  default:
    return 0;
    break;
  }
}


int      
replay_get_value(replay_t r, struct value_line * out)
{
  int ret =replay_queue_pop(r->val_queue, out);
  ((double*)r->nodes[out->id]->userdata)[2] = ((double*)r->nodes[out->id]->userdata)[1];
  ((double*)r->nodes[out->id]->userdata)[1] = ((double*)r->nodes[out->id]->userdata)[0];
  ((double*)r->nodes[out->id]->userdata)[0] = out->value;
  return ret;
}

replay_t
new_replay(const char * filename, hwloc_topology_t topology, int phase)
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
  rp->usleep_len = 50 * BUF_MAX ;
  rp->val_queue = new_replay_queue();
  rp->timestamp_queue = new_replay_queue();
  rp->phase=phase;
  rp->trace_start=-1;

  if(topology==NULL)
    topology_init(&rp->topology);
  else
    hwloc_topology_dup(&rp->topology,topology);

  if(sem_init(&rp->buffer_semaphore, 0, 0)){
    perror("sem_init");
    exit(EXIT_FAILURE);
  }

  if(pthread_mutex_init(&rp->mtx, NULL)){
    perror("pthread_mutex_init");
    exit(EXIT_FAILURE);
  }

  int depth, err=0, i=0, j=0;
  unsigned topo_depth = hwloc_topology_get_depth(rp->topology);

  M_alloc(rp->max,topo_depth,sizeof(double));
  M_alloc(rp->min,topo_depth,sizeof(double));
  M_alloc(rp->nodes,topo_depth*hwloc_get_nbobjs_by_depth(rp->topology,topo_depth-1),sizeof(hwloc_obj_t));
  
  for(j=0;j<hwloc_get_nbobjs_by_depth(rp->topology,topo_depth-1)*topo_depth;j++){
    rp->nodes[i]=NULL;
  }

  for(i=0;i<topo_depth;i++){
    rp->max[i] = DBL_MIN;
    rp->min[i] = DBL_MAX;
  }

  i=0;
  hwloc_obj_t obj = hwloc_get_root_obj (rp->topology);

  /* set fd for updates */
  int update_fds[2];
  if(pipe(update_fds)==-1){
    perror("pipe");
    exit(EXIT_FAILURE);
  }
  rp->update_read_fd = update_fds[0];
  rp->update_write_fd = update_fds[1];

  /* read file once to gather maxs and mins*/
  int looped = 0;
  err=0;
  do{
    union input_line il;
    err = replay_input_paje_line(rp,&il);
    if(err==HL){
      depth = hwloc_get_obj_depth_by_name(rp->topology, il.hl.level);
      obj = hwloc_get_obj_by_depth(rp->topology,depth,il.hl.sibling);
      if(obj==NULL){
	fprintf(stderr, "error while reading trace, can't find obj %s:%u\n",il.hl.level,il.hl.sibling);
	exit(1);
      }
      if(rp->nodes[il.hl.id]==NULL){
	M_alloc(obj->userdata,1,3*sizeof(double));
	rp->nodes[il.hl.id] = obj;
      }
    }
    if(err==VL && il.vl.phase == rp->phase){
      if(rp->trace_start==-1)
	rp->trace_start = il.vl.real_usec;
      obj = rp->nodes[il.vl.id];
      if(obj==NULL){
	fprintf(stderr,"id%d in trace file is not reported in header\n",il.vl.id);
	exit(1);
      }
      if(rp->max[obj->depth] < il.vl.value){
	rp->max[obj->depth] = il.vl.value;
      }
      if(rp->min[obj->depth] > il.vl.value){
	rp->min[obj->depth] = il.vl.value;    
      }
    } 
  } while(err!=-1);
  
  rewind(input);
  return rp;
}

void
delete_replay(replay_t r)
{
  hwloc_obj_t obj;
  unsigned i;
  unsigned topo_depth = hwloc_topology_get_depth(r->topology);
  pthread_cancel(r->fill_thread);
  pthread_cancel(r->timer_thread);
  pthread_join(r->fill_thread,NULL);
  pthread_join(r->timer_thread,NULL);
  sem_destroy(&r->buffer_semaphore);
  pthread_mutex_destroy(&r->mtx);
  delete_replay_queue(r->val_queue);
  delete_replay_queue(r->timestamp_queue);
  for(i=0;i<topo_depth*hwloc_get_nbobjs_by_depth(r->topology,topo_depth-1);i++){
    if(r->nodes[i]!=NULL)
      free(r->nodes[i]->userdata);
  }

  free(r->nodes);
  free(r->max);
  free(r->min);
  fclose(r->input);
  hwloc_topology_destroy(r->topology);
  free(r);
}


void * replay_fill_thread(void* arg){
  replay_t r = (replay_t)arg;
  int err;
  while((err = replay_input_line(r)) >= 0){
    /* buffers are full, so r->usleep_len is necessarily a valid value because there must be around BUF_MAX timestamps queued */
    if(err==0){
      usleep(r->usleep_len);
    }
  }
  pthread_mutex_lock(&r->mtx);
  r->eof=1;
  pthread_mutex_unlock(&r->mtx);
  return NULL;
}

int
replay_is_finished(replay_t r){
  pthread_mutex_lock(&r->mtx);
  int eof = r->eof;
  pthread_mutex_unlock(&r->mtx);
  if(eof==1){
    unsigned sem_count;
    sem_getvalue(&r->buffer_semaphore,&sem_count);
    if(sem_count==0){
      return 1;
    }
  }
  return 0;
}

void * replay_timer_thread(void* arg){
  replay_t r = (replay_t)arg;
  long long trace_elapsed, elapsed, diff;
  struct value_line vl;
  struct timeval start, now, wait;

  gettimeofday(&start,NULL);
  while(!replay_is_finished(r)){
    sem_wait(&r->buffer_semaphore);
    replay_queue_pop(r->timestamp_queue,&vl);
    trace_elapsed = vl.real_usec - r->trace_start;
    gettimeofday(&now,NULL);
    elapsed = 1000000*(now.tv_sec - start.tv_sec) + now.tv_usec -start.tv_usec;
    if((diff = trace_elapsed - elapsed) > 0){
      usleep(diff);
    }
    replay_queue_push(r->val_queue,vl);
    if(write(r->update_write_fd,"1",sizeof(uint64_t))==-1){
      perror("write");
      exit(EXIT_FAILURE);
    }
  }
  printf("end of replay.\n");    
}

void replay_start(replay_t r){
  pthread_create(&r->fill_thread,NULL,replay_fill_thread,r);
  pthread_create(&r->timer_thread,NULL,replay_timer_thread,r);
}

