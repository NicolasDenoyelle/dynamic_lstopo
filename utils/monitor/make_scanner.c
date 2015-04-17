#include <stdio.h>
#include <stdlib.h>

void usage(char** argv){
  fprintf(stdout,"%s <path_to_scanner> \n",argv[0]);
}

void error(const char* msg){
  perror(msg); exit(EXIT_FAILURE);
}

int main(int argc, char** argv){
  FILE * scanner;

  if(argc!=2){
    usage(argv);
    exit(EXIT_SUCCESS);
  }
  scanner = fopen(argv[1],"w");
  if(scanner == NULL){
    error("fopen");
  }

  fprintf(scanner,"%%{\n");
  fprintf(scanner,"#include <stdio.h>\n");
  fprintf(scanner,"#include \"parser.tab.h\"\n");
  fprintf(scanner,"void count();\n");
  fprintf(scanner,"%%}\n");
  fprintf(scanner,"%%option yylineno\n\n");
  fprintf(scanner,"integer      [0-9]+\n");
  fprintf(scanner,"real         {integer}*\\.({integer}+)\n");
  fprintf(scanner,"name         [a-zA-Z0-9_\\-]*\n");
  
  fprintf(scanner,"%%%%\n");
  fprintf(scanner,"\"-\"          { count(); return('-'); };\n");
  fprintf(scanner,"\"+\"          { count(); return('+'); };\n");
  fprintf(scanner,"\"*\"          { count(); return('*'); };\n");
  fprintf(scanner,"\"/\"          { count(); return('/'); };\n");
  fprintf(scanner,"\"(\"          { count(); return('('); };\n");
  fprintf(scanner,"\")\"          { count(); return(')'); };\n");
  fprintf(scanner,"\"}\"          { count(); return('}'); };\n");
  fprintf(scanner,"\"{\"          { count(); return('{'); };\n");
  fprintf(scanner,"\",\"          { count(); return(','); };\n");
  fprintf(scanner,"\":\"          { count(); return(':'); };\n");
  fprintf(scanner,"{integer}      { count(); yylval.str = strdup(yytext); return(INTEGER); };\n");
  fprintf(scanner,"{real}         { count(); yylval.str = strdup(yytext); return(REAL);    };\n");
  fprintf(scanner,"{name}         { count(); yylval.str = strdup(yytext); return(NAME);    };\n");
  fprintf(scanner,"\\n          { count();}\n");
  fprintf(scanner,"#.*         { count();}\n");
  fprintf(scanner,".           { count();}\n");
  fprintf(scanner,"\n%%%%\n\n");
  fprintf(scanner,"int yywrap() {\nreturn 1;\n}\n");
  fprintf(scanner,"int column = 0;\n");
  fprintf(scanner,"void count() {\n");
  fprintf(scanner,"   int i;\n");
  fprintf(scanner,"   for (i = 0; yytext[i] != '\\0'; i++) {\n");
  fprintf(scanner, "      if (yytext[i] == '\\n')\n");
  fprintf(scanner,"         column = 0;\n");
  fprintf(scanner,"      else if (yytext[i] == '\\t')\n");
  fprintf(scanner,"         column += 8 - (column %% 8);\n");
  fprintf(scanner,"      else\n");
  fprintf(scanner,"         column++;\n");
  fprintf(scanner,"   }\n");
  fprintf(scanner,"}\n\n");

  fclose(scanner);
  return EXIT_SUCCESS;
}
