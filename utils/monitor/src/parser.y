%{
#include <stdlib.h>
#include <unistd.h>
#include <string.h>     
#include <dlfcn.h>
#include <papi.h>
#include <hwloc.h>
#include "parser.h"

  int yyerror(char * s);
  char * concat_expr(char* expr1, char sign, char* expr2);
  char * parenthesis(char* expr);
  int strsearch(char * key, char** array, unsigned int array_len);
  void print_func(char * name,char* code);
  unsigned int nb_counters, nb_monitors, topodepth, i;
  char * err;
  char ** event_names, ** monitor_names;
  int * monitor_depths;
  

%}

%token <str> NAME COUNTER REAL INTEGER DEPTH
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
: NAME '{' hwloc_obj_expr ',' add_expr '}' {
  /* {{{ */
  if(nb_monitors==0 || 
     //     !bsearch(&tmp, monitors,nb_monitors,sizeof(Monitor_t),cmpmonitordepth) ||
     strsearch($1, monitor_names,nb_monitors)==-1){
    print_func($1,$5);
    monitor_names[nb_monitors]=strdup($1);
    nb_monitors++;
  }
  else{
    fprintf(stderr,"monitor \"%s\" ignored because its name is already used by another one\n",$1);
  }
  /* }}} */
 }
;

add_expr
: mul_expr              {$$ = $1;}
| add_expr '+' mul_expr {$$=concat_expr($1,'+',$3);}
| add_expr '-' mul_expr {$$=concat_expr($1,'-',$3);}
;
   
 mul_expr
: primary_expr              {$$ = $1;}
| mul_expr '*' primary_expr {$$=concat_expr($1,'*',$3);}
| mul_expr '/' primary_expr {$$=concat_expr($1,'/',$3);}
;

hwloc_obj_expr
: DEPTH ':' INTEGER {monitor_depths[nb_monitors]=atoi($3);}
;

primary_expr 
: NAME    {$$ = $1;}
| COUNTER {
   int counter_idx=-1;
   if(nb_counters==0){
     event_names[0] = strdup($1);
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
       event_names[nb_counters]=strdup($1);
       char var[1024];
       sprintf(var,"in[%d]",nb_counters);
       $$=strdup(var);
       nb_counters++;
     }
     else{
       char var[1024];
       sprintf(var,"in[%d]",counter_idx);
       $$=strdup(var);
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

int yyerror (char *s) {
  /* {{{ */

  fflush (stdout);
  fprintf (stderr, "%d:%d: %s\n", yylineno, column, s);
  exit(EXIT_FAILURE);

  /* }}} */
}

char * concat_expr(char* expr1, char sign, char* expr2){
  /* {{{ */

  char* str =  malloc(strlen(expr1)+strlen(expr2)+2); 
  if(str==NULL)
    return NULL;
  str[0]='\0';  
  strcat(str,expr1);
  str[strlen(expr1)]=sign;
  str[strlen(expr1)+1]='\0';
  strcat(str,expr2);
  free(expr1);
  free(expr2);
  return str;

  /* }}} */
}

char * parenthesis(char* expr){
/* {{{ */
  char * str = malloc(strlen(expr)+3);
  if(str==NULL)
    return NULL;
  str[0]='(';
  str[1]='\0';
  strcat(str,expr);
  str[strlen(expr)+1]=')';
  str[strlen(expr)+2]='\0';
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

void print_func(char * name,char* code){
  /* {{{ */
  FILE  * header = fopen(PARSED_CODE_SRC,"a+");
  if(header==NULL){
    fflush(stdout);
    fprintf(stderr,"could not create or open %s\n",PARSED_CODE_SRC);
    exit(1);
  }
  fprintf(header,"double %s(long long * in){\n\treturn (double) %s;\n}\n\n",name,code);
  fclose(header);

  /* }}} */
}





int parser(const char * file_name) {
  hwloc_topology_t topology;
  /* Allocate and initialize topology object. */
  hwloc_topology_init(&topology);
  /* Perform the topology detection. */
  hwloc_topology_load(topology);
  topodepth = hwloc_topology_get_depth(topology);

  monitor_names = malloc(sizeof(char*)*topodepth);
  monitor_depths = malloc(sizeof(int)*topodepth);
  event_names = malloc(sizeof(char*)*PAPI_MAX_MPX_CTRS);
  if(event_names==NULL || monitor_names==NULL || monitor_depths==NULL){
    exit(1);
  }

  /* prepare file for functions copy */
  remove(PARSED_CODE_SRC);
  FILE * output = fopen (PARSED_CODE_SRC, "a+");
  if(output==NULL){
    fprintf (stderr, "%s: Could not create %s\n", file_name,PARSED_CODE_SRC);
    exit(1);
  }
  fprintf(output,"#include <stdlib.h>\n");
  fprintf(output,"#include <stdio.h>\n");
  fprintf(output,"#include <string.h>\n");
  fprintf(output,"char ** event_names;\n");
  fprintf(output,"unsigned int n_events;\n");
  fprintf(output,"unsigned int n_monitors;\n");
  fprintf(output,"char ** monitor_names;\n");
  fprintf(output,"int  *  monitor_depths;\n");
  fflush(output);

  /* parsing input file and creating functions.c file */
  FILE *input = NULL;
  if (file_name!=NULL) {
    input = fopen (file_name, "r");
    if (input) {
      yyin = input;
      yyparse();
    }
    else {
      fprintf (stderr, "Could not open %s\n", file_name);
      return 1;
    }
  }
  else {
    fprintf (stderr, "error: invalid input file name\n");
    return 1;
  }

  /* print into shared lib the initialization and cleanup functions */
  fprintf(output,"void __attribute__ ((constructor)) monitorlib_init(void){\n");
  fprintf(output,"\tn_events = %d;\n",nb_counters);
  fprintf(output,"\tn_monitors = %d;\n",nb_monitors);
  fprintf(output,"\tevent_names = malloc(sizeof(char*)*%d);\n",nb_counters);
  fprintf(output,"\tmonitor_names = malloc(sizeof(char*)*%d);\n",nb_monitors);
  fprintf(output,"\tmonitor_depths = malloc(sizeof(int)*%d);\n",nb_monitors);
  fprintf(output,"\tif(event_names==NULL || monitor_names ==NULL || monitor_depths==NULL){\n");
  fprintf(output,"\t\tfprintf(stderr,\"nb_counters:%d\\n\");\n",nb_counters);
  fprintf(output,"\t\tfprintf(stderr,\"nb_monitors:%d\\n\");\n",nb_monitors);
  fprintf(output,"\t\tfprintf(stderr,\"malloc error\\n\");\n");
  fprintf(output,"\t\texit(EXIT_FAILURE);\n");
  fprintf(output,"\t}\n");
  for(i=0;i<nb_counters;i++){
    fprintf(output,"\tevent_names[%d]=strdup(\"%s\");\n",i, event_names[i]);
  }
  for(i=0;i<nb_monitors;i++){
    fprintf(output,"\tmonitor_names[%d]=strdup(\"%s\");\n",i, monitor_names[i]);
  }
  for(i=0;i<nb_monitors;i++){
    fprintf(output,"\tmonitor_depths[%d]=%d;\n",i, monitor_depths[i]);
  }
  fprintf(output,"}\n\n");

  /* a bit of cleanup */
  free(event_names);
  free(monitor_names);
  free(monitor_depths);
  fclose(output);

  /* create shared library with monitors.c */
  char command[1024]; command[0]='\0'; 
  sprintf(command,"gcc -shared -fpic %s -o %s",PARSED_CODE_SRC,PARSED_CODE_LIB);
  system(command);

  hwloc_topology_destroy(topology);
  return 0;
}

/**
 * Main test function to test parser.
 * Uncomment and make parser to test.
 */
int main_parser(int argc, char* argv[]){
  if(argc != 2){
    printf("1 argument required: path to file to parse\n");
    return 0;
  }
  parser(argv[1]);

  /* get current working dir */
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) == NULL){
    perror("getcwd() error");
    return 1;
  }
  char * lib = malloc(strlen(PARSED_CODE_LIB)+strlen(cwd)+2);
  lib[0]='\0';
  strcat(strcat(strcat(lib,cwd),"/"),PARSED_CODE_LIB);
  /* load shared libraries */
  void * dlhandle = dlopen(lib,RTLD_NOW | RTLD_GLOBAL);
  //  free(lib);
  if(dlhandle==NULL){
    fprintf(stderr,"%s\n",dlerror());
    return 1;
  }

  char * err;
  char ** m_names = *(char ***)dlsym(dlhandle,"monitor_names");
  if((err=dlerror())!=NULL){
    fprintf(stderr,"error while loading monitors: %s\n",err);
    dlclose(dlhandle);
    return 1;
  }

  int     n_m     = *(int*)    dlsym(dlhandle,"n_monitors");
  if((err=dlerror())!=NULL){
    fprintf(stderr,"error while loading monitors: %s\n",err);
    dlclose(dlhandle);
    return 1;
  }

  fprintf(stdout,"n_events=%d\n",nb_counters);
  while(n_m--){
    fprintf(stdout,"%s\n",m_names[n_m]);
  }

  return 1;
}
