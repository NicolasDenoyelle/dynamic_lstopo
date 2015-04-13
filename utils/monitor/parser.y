%{
#include <stdlib.h>
#include <unistd.h>
#include <string.h>     
#include <dlfcn.h>
#include <papi.h>
#include "hwloc.h"
#include "monitor_utils.h"

  hwloc_topology_t topology;
  int yyerror(char * s);
  char * concat_expr(char* expr1, char sign, char* expr2);
  char * parenthesis(char* expr);
  int strsearch(char * key, char** array, unsigned int array_len);
  void print_func(char * name,char* code);
  unsigned int nb_counters, nb_monitors, topodepth, i;
  char * err;
  char ** event_names, ** monitor_names, ** monitor_obj;  
  FILE * tmp;

%}

%error-verbose
%token <str> NAME COUNTER REAL INTEGER OBJ
%type  <str> primary_expr add_expr mul_expr 


%union{
  char * str;
 }

%start monitor_list
%%

monitor_list
: monitor
| monitor monitor_list
;

monitor
: NAME '{' hwloc_obj ',' add_expr '}' {
  /* {{{ */
  if(nb_monitors==0 || 
     strsearch($1, monitor_names,nb_monitors)==-1){
    print_func($1,$5);
    monitor_names[nb_monitors]=$1;
    nb_monitors++;
  }
  else{
    fprintf(stderr,"monitor \"%s\" ignored because its name is already used by another one\n",$1);
  }
  free($5);
  /* }}} */
 }
;

add_expr
: mul_expr              {$$ = $1;}
| add_expr '+' mul_expr {$$=concat_expr($1,'+',$3); free($1); free($3);}
| add_expr '-' mul_expr {$$=concat_expr($1,'-',$3); free($1); free($3);}
;
   
 mul_expr
: primary_expr              {$$ = $1;}
| mul_expr '*' primary_expr {$$=concat_expr($1,'*',$3); free($1); free($3);}
| mul_expr '/' primary_expr {$$=concat_expr($1,'/',$3); free($1); free($3);}
;

hwloc_obj
: OBJ {monitor_obj[nb_monitors]=$1;}
;

primary_expr 
: COUNTER {
   int counter_idx=-1;
   if(nb_counters==0){
     event_names[0] = $1;
     nb_counters=1;
     $$=strdup("in[0]");
   }
   else{
     counter_idx = strsearch($1,event_names,nb_counters);
     if(counter_idx==-1){
       if(nb_counters>=PAPI_MAX_MPX_CTRS){
       	 fprintf(stderr,"Too many counters, max=%d\n",PAPI_MAX_MPX_CTRS);
       	 free(event_names);
       	 exit(1);
       }
       event_names[nb_counters]=$1;
       char var[1024];
       sprintf(var,"in[%d]",nb_counters);
       $$=strdup(var);
       nb_counters++;
     }
     else{
       char var[1024];
       sprintf(var,"in[%d]",counter_idx);
       $$=strdup(var);
       free($1);
     }
   }
  }
| REAL    {$$ = $1;}
| INTEGER {$$ = $1;}
;

%%

extern char yytext[];
extern FILE *yyin;
extern int column;
extern int yylineno;

void print_func(char * name,char* code){
  /* {{{ */
  fprintf(tmp,"double %s(long long * in){\n\treturn (double) %s;\n}\n\n",name,code);
  /* }}} */
}


int yyerror (char *s) {
  /* {{{ */
  fflush (stdout);
  
  fprintf(stderr, "%d:%d: %s while scanning input file\n", yylineno, column, s);
  fprintf(stderr, "available hwloc_objs: \n");
  unsigned nobjs;
  char** objs = get_avail_hwloc_objs_names(&nobjs);
  while((nobjs--) >0){
    fprintf(stderr,("\t%s\n"),objs[nobjs]);
    free(objs[nobjs]);
  }

  fprintf(stderr, "available counters: \n");
  unsigned ncount;
  char** counters = get_avail_papi_counters(&ncount);
  while((ncount--) >0){
    fprintf(stderr,("\t%s\n"),counters[ncount]);
    free(counters[ncount]);
  }
  
  exit(EXIT_FAILURE);

  /* }}} */
}

char * concat_expr(char* expr1, char sign, char* expr2){
  /* {{{ */

  char* str =  malloc(strlen(expr1)+strlen(expr2)+2);
  if(str==NULL)
    {
      perror("malloc failed");
      exit(EXIT_FAILURE);
    }
  memset(str,0,strlen(expr1)+strlen(expr2)+2);
  sprintf(str,"%s%c%s",expr1,sign,expr2);
  return str;

  /* }}} */
}

char * parenthesis(char* expr){
/* {{{ */
  char * str = malloc(strlen(expr)+3);
  if(str==NULL)
    return NULL;
  memset(str,0,strlen(expr)+3);
  sprintf(str,"(%s)",expr);
  return str;
/* }}} */
}

int strsearch(char* key, char** array, unsigned int size){
  while(size--){
    if(!strcmp(array[size],key))
      return size;
  }
  return -1;
}

int cmpstr(void const *a, void const *b) { 
    const char *key = a;
    const char * const *arg = b;
    printf("myStrCmp: s1(%p): %s, s2(%p): %s\n", a, key, b, *arg);
    return strcmp(key, *arg);
}

struct parsed_names * parser(const char * file_name) {
  /* Allocate and initialize topology object. */
  topology_init(&topology);
  topodepth = hwloc_topology_get_depth(topology);

  monitor_names = malloc(sizeof(char*)*topodepth);
  monitor_obj  = malloc(sizeof(char*)*topodepth);
  event_names   = malloc(sizeof(char*)*PAPI_MAX_MPX_CTRS);
  if(event_names==NULL || monitor_names==NULL || monitor_obj==NULL){
    exit(1);
  }

  /* prepare file for functions copy */
  char * tmp_name = malloc(strlen("/tmp/tmp.XXXXXX.c")+1);
  memset(tmp_name,0,strlen("/tmp/tmp.XXXXXX.c")+1);
  sprintf(tmp_name,"/tmp/tmp.XXXXXX.c");
  if(mkstemps(tmp_name,2)==-1){
    perror("mkstemps");
    exit(EXIT_FAILURE);
  }

  tmp = fopen(tmp_name,"a+");
  if(tmp==NULL){
    fprintf (stderr, "%s: Could not create a temporary file %s\n", file_name, tmp_name);
    exit(1);
  }

  /* parsing input file and creating functions.c file */
  FILE *input = NULL;
  if (file_name!=NULL) {
    input = fopen (file_name, "r");
    if (input) {
      yyin = input;
      yyparse();
      fclose(input);
    }
    else {
      fprintf (stderr, "Could not open %s\n", file_name);
      return NULL;
    }
  }
  else {
    fprintf (stderr, "error: invalid input file name\n");
    return NULL;
  }
  fclose(tmp);
  hwloc_topology_destroy(topology);

  struct parsed_names * pn = malloc(sizeof(*pn));
  if(pn==NULL){
    perror("malloc");
    exit(1);
  }


  
  pn->libso_path = malloc(strlen(tmp_name)+1);
  if(pn->libso_path==NULL){
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  memset(pn->libso_path,0,sizeof(pn->libso_path)+1);
  strncpy(pn->libso_path,tmp_name,strlen(tmp_name));
  pn->libso_path[strlen(tmp_name)-1] = 's';
  pn->libso_path[strlen(tmp_name)] = 'o';

  pn->n_events = nb_counters;
  pn->n_monitors = nb_monitors;
  pn->monitor_names = monitor_names;
  pn->event_names = event_names;
  pn->monitor_obj = monitor_obj;

  /* create shared library with monitors.c */
  char command[1024]; command[0]='\0'; 
  sprintf(command,"gcc -shared -fpic -rdynamic %s -o %s",tmp_name, pn->libso_path);
  system(command);
  remove(tmp_name);
  free(tmp_name);
  return pn;
}

