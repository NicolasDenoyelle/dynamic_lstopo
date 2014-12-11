$LEX=lex
YACC=bison
CFLAGS=-Wall
CC=gcc 
OPTIONS= -ggdb
LFLAGS= -lhwloc -L/opt/papi-5.3.0/lib/ -lpapi -ldl -lpthread
SCRIPTS_DIR=./scripts
CURRENT_DIR = $(shell pwd)
SRC=src
BUILD=build

all: $(BUILD) main

$(BUILD):
	mkdir -p $@

main: $(SRC)/main.c $(BUILD)/parser.o $(BUILD)/scanner.o $(BUILD)/monitor.o
	$(CC) $(OPTIONS) $(CFLAGS) $^ -o $@ $(LFLAGS)

parser: $(SRC)/parser.c $(BUILD)/scanner.o
	sed -i "s/main_parser(/main(/" $(SRC)/parser.c
	$(CC) $(OPTIONS) -Wno-implicit-function-declaration $(CFLAGS) $(SRC)/parser.c $(BUILD)/scanner.o -o $@ $(LFLAGS)

$(BUILD)/monitor.o: $(SRC)/monitor.c
	$(CC) $(OPTIONS) $(CFLAGS) -c $^ -o $@ 

$(BUILD)/parser.o: $(SRC)/parser.c
	sed -i "s/main(/main_parser(/" $^
	$(CC) $(OPTIONS) -Wno-implicit-function-declaration $(CFLAGS) -c $(SRC)/parser.c -o $@ $(LFLAGS)

$(SRC)/scanner.l: 
	sh $(SCRIPTS_DIR)/make_scanner.sh $@

$(SRC)/scanner.c: $(SRC)/scanner.l
	$(LEX) -o $@ $^

$(BUILD)/scanner.o: $(SRC)/scanner.c
	$(CC) $(OPTIONS) -Wno-unused-function $(CFLAGS) -c $^ -o $@ -lfl

$(SRC)/parser.c: $(SRC)/parser.y
	$(YACC) -o $@ --defines=$(SRC)/parser.tab.h $^

clean:
	rm $(BUILD)/*

very_clean:
	rm $(BUILD)/* $(SRC)/custom_monitors.c #$(SRC)/scanner.c $(SRC)/parser.tab.h $(SRC)/parser.c 
