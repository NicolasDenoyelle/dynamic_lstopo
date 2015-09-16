#define __USE_GNU
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <omp.h>
#include <math.h>
#include <ncurses.h>
#include <hwloc.h>
#include <hwloc/helper.h>
#include "linked_list.h"

#define RAND 1
hwloc_topology_t topo;
uint64_t online_PU;
uint64_t L1_s, L2_s, L3_s, MEM_s;

void init()
{
  FILE * cmd_out;
  size_t n;
  char * lineptr;
  

  hwloc_topology_init(&topo);
  hwloc_topology_load(topo);

  L1_s = hwloc_get_obj_by_depth(topo, hwloc_get_cache_type_depth (topo, 1, HWLOC_OBJ_CACHE_DATA), 0)->attr->cache.size;
  L2_s = hwloc_get_obj_by_depth(topo, hwloc_get_cache_type_depth (topo, 2, HWLOC_OBJ_CACHE_DATA), 0)->attr->cache.size;
  L3_s = hwloc_get_obj_by_depth(topo, hwloc_get_cache_type_depth (topo, 3, HWLOC_OBJ_CACHE_DATA), 0)->attr->cache.size;
  MEM_s = hwloc_get_obj_by_type (topo, HWLOC_OBJ_NODE , 0)->memory.local_memory;

  online_PU = hwloc_bitmap_weight(hwloc_topology_get_topology_cpuset(topo));

  MEM_s/=(2*online_PU);

  initscr();
  keypad(stdscr, TRUE);
  printw("KEYS:\n");
  printw(" <- left_arrow = lower list size.\n");
  printw(" -> right_arrow = increase list size.\n");
  printw("    up_arrow = nav up.\n");
  printw("    down_arrow = nav_down.\n");
  refresh();
}

#define CONTROL_MAX(a,b) a>b?a:b
#define CONTROL_MIN(a,b) a>b?b:a
#define CONTROLS_START_Y 6
#define CONTROLS_START_X 0
#define CONTROL_WIDTH COLS
#define CONTROL_HEIGHT 3
#define CURSOR_WIDTH 5
#define PU_WIDTH 7
#define GAUGE_WIDTH CONTROL_WIDTH-CURSOR_WIDTH-PU_WIDTH

static int compute_gauge_fill(uint64_t fill, uint64_t fill_min, uint64_t fill_max){
  double start = ((double)(log(fill_min)*GAUGE_WIDTH)/(double)(log(fill_max)-log(fill_min)));
  double end = ((double)(log(fill)*GAUGE_WIDTH)/(double)(log(fill_max)-log(fill_min)));
  return (int)(end-start);
}

struct control{
  WINDOW * gauge_w;
  WINDOW * PU_w;
  WINDOW * cursor_w;

  int PU;
  int cursor;
  linked_list list;
  uint64_t list_size;
  int rand_links;
};

struct control * 
new_control(int startx, int starty, int PU){
  struct control * control = malloc(sizeof(*control));
  control->gauge_w  = newwin(CONTROL_HEIGHT,  GAUGE_WIDTH, starty, startx);
  control->PU_w     = newwin(CONTROL_HEIGHT,     PU_WIDTH, starty, startx+GAUGE_WIDTH);
  control->cursor_w = newwin(CONTROL_HEIGHT, CURSOR_WIDTH, starty, startx+GAUGE_WIDTH+PU_WIDTH);
  
  wborder(control->gauge_w , '|', '|', '-','-','+','+','+','+');
  wborder(control->PU_w    , ' ', ' ', '-','-','-','-','-','-');
  wborder(control->cursor_w, '|', '|', '-','-','+','+','+','+');
  
  mvwprintw(control->PU_w, 1, 1, "PU#%d",PU); 

  control->cursor=0;
  control->PU=PU;
  control->list_size=L1_s;
  mvwprintw(control->gauge_w,1,0, "%*s",compute_gauge_fill(L1_s, L1_s/4, MEM_s), "|"); 

  control->list = new_linked_list(MEM_s, L1_s/sizeof(uint64_t), RAND);

  wrefresh(control->gauge_w);
  wrefresh(control->PU_w);
  wrefresh(control->cursor_w);    
  
  return control;
}

inline void control_enable(struct control * control){
  mvwprintw(control->cursor_w,1,2, "*"); 
  wrefresh(control->cursor_w);  
}

inline void control_disable(struct control * control){
  mvwprintw(control->cursor_w,1,2, " "); 
  wrefresh(control->cursor_w);  
}

void 
control_gauge_set(struct control * control, uint64_t size)
{
  if(size>MEM_s || size <L1_s/4)
    return;
  uint64_t old_n_elem = control->list_size/sizeof(*control->list);
  control->list_size = size;
  uint64_t n_elem = size/sizeof(*control->list);
  int fill = compute_gauge_fill(size, L1_s/4, MEM_s);
  mvwprintw(control->gauge_w, 1, 0, "%*s%*s",fill,"|",GAUGE_WIDTH-fill," ");
  wrefresh(control->gauge_w);
  resize_linked_list(control->list, old_n_elem, n_elem, RAND);  
}

inline void control_gauge_fill(struct control * control){
  control_gauge_set(control, control->list_size*2);
}

inline void control_gauge_empty(struct control * control){
  control_gauge_set(control, control->list_size/2);
}

void 
delete_control(struct control * control)
{
  wborder(control->cursor_w, ' ', ' ', ' ',' ',' ',' ',' ',' ');
  wborder(control->gauge_w, ' ', ' ', ' ',' ',' ',' ',' ',' ');
  wborder(control->PU_w, ' ', ' ', ' ',' ',' ',' ',' ',' ');
  wrefresh(control->cursor_w);
  wrefresh(control->gauge_w);
  wrefresh(control->PU_w);
  delwin(control->cursor_w);
  delwin(control->gauge_w);
  delwin(control->PU_w);
  delete_linked_list(control->list);
  free(control);
}

struct controls{
  unsigned n_controls;
  unsigned alloc_controls;
  struct control ** control;
  unsigned cursor_pos;
  WINDOW * layout;
};


struct controls * 
new_controls(void)
{
  struct controls * controls = malloc(sizeof(*controls));
  controls->n_controls=0;
  controls->alloc_controls = 64;
  controls->control = malloc(64*sizeof(*controls->control));
  controls->cursor_pos=0;
  controls->layout = newwin(CONTROL_HEIGHT, GAUGE_WIDTH, CONTROLS_START_Y, CONTROLS_START_X);
  wborder(controls->layout, '|', '|', '-','-','+','+','+','+');
  mvwprintw(controls->layout,1,compute_gauge_fill(L1_s,L1_s/4, MEM_s),"L1");
  mvwprintw(controls->layout,1,compute_gauge_fill(L2_s,L1_s/4, MEM_s),"L2");
  mvwprintw(controls->layout,1,compute_gauge_fill(L3_s,L1_s/4, MEM_s),"L3");
  wrefresh(controls->layout);  
  return controls;
}

void
controls_add_control(struct controls * controls){
  if(controls->n_controls <= controls->alloc_controls){
    controls->alloc_controls*=2;
    controls->control = realloc(controls->control,controls->alloc_controls*sizeof(*(controls->control)));
  }
  unsigned controls_start_y = CONTROLS_START_Y + CONTROL_HEIGHT + controls->n_controls*CONTROL_HEIGHT;
  controls->control[controls->n_controls] = new_control(CONTROLS_START_X, controls_start_y, controls->n_controls);
  if(controls->cursor_pos==controls->n_controls)
    control_enable(controls->control[controls->n_controls]);
  controls->n_controls++;
}

void controls_cursor_up(struct controls * controls){
  control_disable(controls->control[controls->cursor_pos]);
  controls->cursor_pos = (controls->cursor_pos-1)%controls->n_controls;
  control_enable(controls->control[controls->cursor_pos]);
}

void controls_cursor_down(struct controls * controls){
  control_disable(controls->control[controls->cursor_pos]);
  controls->cursor_pos = (controls->cursor_pos+1)%controls->n_controls;
  control_enable(controls->control[controls->cursor_pos]);
}

void
delete_controls(struct controls * controls)
{
  unsigned i;
  for(i=0;i<controls->n_controls; i++){
    delete_control(controls->control[i]);
  }
  free(controls->control);
  free(controls);
}

inline void usage(char * argv0){
  fprintf(stdout,"%s -s <data_set (use multiple times) -r <repeat> >\n",argv0);
}

/* int  */
/* main(int argc, char**argv) */
/* { */
/*   init(); */
/*   linked_list list = new_linked_list(MEM_s, L3_s*64/sizeof(*list), RAND); */
/*   printf("start size = %lu, L3 size = %lu\n", L3_s*2, L3_s*2); */
/*   uint64_t start=0; */
/*   while(1) */
/*     walk_linked_list(list,&start,L3_s*2/sizeof(*list)); */
/*   return 0; */
/* } */

int
main(int argc, char**argv)
{
  uint64_t i;
  int stop_flag = 0;
  init();
  omp_set_num_threads(online_PU+1);
  struct controls * controls = new_controls();
  for(i=0;i<online_PU;i++)
    controls_add_control(controls);

#pragma omp parallel shared(controls, stop_flag)
  {
    if(omp_get_thread_num()==online_PU)
      {
	int ch;
	while((ch=getch())!='q'){
	  switch(ch){
	  case KEY_UP:
	    controls_cursor_up(controls);
	    break;
	  case KEY_DOWN:
	    controls_cursor_down(controls);
	    break;
	  case KEY_LEFT:
	    control_gauge_empty(controls->control[controls->cursor_pos]);
	    break;
	  case KEY_RIGHT:
	    control_gauge_fill(controls->control[controls->cursor_pos]);
	    break;
	  default:
	    break;
	  }
	}
	stop_flag=1;
      }
    else{
      hwloc_bitmap_t cpuset = hwloc_bitmap_alloc();
      hwloc_bitmap_set(cpuset,omp_get_thread_num()%online_PU);
      hwloc_set_cpubind(topo, cpuset, HWLOC_CPUBIND_THREAD|HWLOC_CPUBIND_STRICT);
      uint64_t marker=0;
      struct control * control = controls->control[omp_get_thread_num()];
      while(!stop_flag){
	walk_linked_list(control->list, &marker, L3_s);
      }
      hwloc_bitmap_free(cpuset);
    }
  }
  
  hwloc_topology_destroy(topo);  
  delete_controls(controls);
  endwin();			/* End curses mode		  */
  return 0;
}

