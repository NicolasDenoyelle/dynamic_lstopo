#!/bin/sh

if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
fi

outfile=$1

echo "%{"                                                                             > $outfile
echo "#include <stdio.h>"                                                            >> $outfile
echo "#include \"parser.tab.h\""                                                     >> $outfile
echo "void count();"                                                                 >> $outfile
echo "%}"                                                                            >> $outfile
echo "%option yylineno"                                                              >> $outfile
printf "integer    [0-9]+\n"                                                         >> $outfile
printf "real    {integer}*"\\\."({integer}+)\n"                                      >> $outfile
printf "depth depth\n"                                                               >> $outfile
printf "counter  "                                                                   >> $outfile
papi_avail | awk '{if($3=="Yes" || $3=="No")print $1}'| awk '{if(NR!=1){printf "|"} printf $1}' >> $outfile
echo ""                                                                              >> $outfile
printf "name           [a-zA-Z0-9_]+\n"                                              >> $outfile
echo ""                                                                              >> $outfile
echo "%%"                                                                            >> $outfile
printf "\"-\"   { count(); return('-'); };\n"                                        >> $outfile
printf "\"+\"   { count(); return('+'); };\n"                                        >> $outfile
printf "\"*\"   { count(); return('*'); };\n"                                        >> $outfile
printf "\"/\"   { count(); return('/'); };\n"                                        >> $outfile
printf "\"(\"   { count(); return('('); };\n"                                        >> $outfile
printf "\")\"   { count(); return(')'); };\n"                                        >> $outfile
printf "\"{\"   { count(); return('{'); };\n"                                        >> $outfile
printf "\"}\"   { count(); return('}'); };\n"                                        >> $outfile
printf "\":\"   { count(); return(':'); };\n"                                        >> $outfile
printf "\",\"   { count(); return(','); };\n"                                        >> $outfile
printf "{integer}   { count(); yylval.str = strdup(yytext); return(INTEGER); };\n"   >> $outfile
printf "{real}      { count(); yylval.str = strdup(yytext); return(REAL); };\n"      >> $outfile
printf "{counter}   { count(); yylval.str = strdup(yytext); return(COUNTER); };\n"   >> $outfile
printf "{depth}     { count(); yylval.str = strdup(yytext); return(DEPTH); };\n"      >> $outfile
printf "{name}      { count(); yylval.str = strdup(yytext); return(NAME); };\n"      >> $outfile
printf "\\\n    {count();}\n"                                                        >> $outfile
printf "#.*   {count();}\n"                                                          >> $outfile
printf ".     {count();}\n"                                                          >> $outfile
echo "%%"                                                                            >> $outfile
printf "int yywrap() {\nreturn 1;\n}\n"                                              >> $outfile
printf "int column = 0;\n"                                                           >> $outfile
printf "void count() {\n"                                                            >> $outfile
printf "   int i;\n"                                                                 >> $outfile
printf "   for (i = 0; yytext[i] != '\\\0'; i++) {\n"                                >> $outfile
printf "      if (yytext[i] == '\\\n')\n"                                            >> $outfile
printf "         column = 0;\n"                                                      >> $outfile
printf "      else if (yytext[i] == '\\\t')\n"                                       >> $outfile
printf "         column += 8 - (column %% 8);\n"                                     >> $outfile
printf "      else\n"                                                                >> $outfile
printf "         column++;\n"                                                        >> $outfile
printf "   }\n"                                                                      >> $outfile
printf "}\n"                                                                         >> $outfile


