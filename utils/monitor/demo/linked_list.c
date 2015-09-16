#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linked_list.h"

#define M_alloc(pointer,n_elem,size)		\
  if((pointer=malloc(n_elem*size))==NULL){	\
    perror("malloc");				\
    exit(EXIT_FAILURE);				\
  }						\
  memset(pointer,0,n_elem*size);

uint64_t *
new_rand_links(uint64_t n)
{
  uint64_t * links, * rands, val;
  unsigned i, seed = 44567754;
  
  M_alloc(links,n,sizeof(uint64_t));
  M_alloc(rands,n,sizeof(uint64_t));
  for(i=0;i<n;i++){
    rands[i] = i;
  }
  for(i=n;i>0;i--){
    val = rand_r(&seed)%i;
    links[i-1] = rands[val];
    rands[val] = rands[i-1];
  }
  free(rands);
  return links;
}

linked_list
new_linked_list(uint64_t max_size, uint64_t n_elem, int rand)
{
  linked_list list;
  M_alloc(list,1,max_size);
  uint64_t * links;

  if(rand){
    links = new_rand_links(n_elem);
    memcpy(list,links,n_elem*sizeof(*list));
  }
  else{
    M_alloc(links,max_size/sizeof(uint64_t),sizeof(uint64_t));
    uint64_t i;
    for(i=0;i<max_size/sizeof(uint64_t);i++)
      links[i]=i+1;
    links[i] = 0;
    links[n_elem-1]=0;
    memcpy(list,links,max_size);
  }
 
  free(links);
  return list;
}

void
resize_linked_list(linked_list list, uint64_t old_n_elem, uint64_t n_elem, int rand)
{
  if(!rand){
    list[old_n_elem-1] = old_n_elem;
    list[n_elem-1]=0;
  }
  else{
    uint64_t * links = new_rand_links(n_elem);
    memcpy(list,links,n_elem*sizeof(*list));
    free(links);
  }
}

inline void
delete_linked_list(linked_list list)
{
  free(list);
}

void
walk_linked_list(linked_list list, uint64_t * start, unsigned n_step)
{
  //#include "walks.h"
  while((n_step--)>0){
  *start=list[*start];
}
  
}

