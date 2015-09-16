#include <inttypes.h>

typedef uint64_t * linked_list;

uint64_t *           new_rand_links(uint64_t n);
linked_list          new_linked_list(uint64_t max_size, uint64_t n_elem, int rand);
void                 resize_linked_list(linked_list list, uint64_t old_n_elem, uint64_t n_elem, int rand);
void                 delete_linked_list(linked_list list);
void                 walk_linked_list(linked_list list, uint64_t * start, unsigned n_step);
