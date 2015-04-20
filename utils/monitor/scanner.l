%{
#include <stdio.h>
#include "parser.tab.h"
void count();
%}
%option yylineno

integer      [0-9]+
real         {integer}*\.({integer}+)
name         [a-zA-Z0-9_\-]*
mask         {name}={integer}
COLON        ":"
mask_sep     {COLON}*
%%
"-"          { count(); return('-'); };
"+"          { count(); return('+'); };
"*"          { count(); return('*'); };
"/"          { count(); return('/'); };
"("          { count(); return('('); };
")"          { count(); return(')'); };
"}"          { count(); return('}'); };
"{"          { count(); return('{'); };
","          { count(); return(','); };
{integer}    { count(); yylval.str = strdup(yytext); return(INTEGER); };
{real}       { count(); yylval.str = strdup(yytext); return(REAL);    };
{name}       { count(); yylval.str = strdup(yytext); return(NAME);    };
{mask}       { count(); yylval.str = strdup(yytext); return(MASK);    }
{mask_sep}   { count(); yylval.str = strdup(yytext); return(MASK_SEPARATOR);};
\n           { count();}
#.*          { count();}
.            { count();}

%%

int yywrap() {
return 1;
}
int column = 0;
void count() {
   int i;
   for (i = 0; yytext[i] != '\0'; i++) {
      if (yytext[i] == '\n')
         column = 0;
      else if (yytext[i] == '\t')
         column += 8 - (column % 8);
      else
         column++;
   }
}

