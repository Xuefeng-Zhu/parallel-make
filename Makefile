CC=gcc
CFLAGS=-O0 -Wall -Wextra -Werror -Wno-unused -Wno-sign-compare -g -std=c99 -pthread

all: parmake

doc/html: parmake.c
	doxygen doc/Doxyfile 2> /dev/null
	cp doc/flow.png doc/html/

parmake: parmake.o queue.o parser.o rule.o
	$(CC) $(CFLAGS) parmake.o queue.o parser.o rule.o -o parmake

parmake.o: parmake.c
	$(CC) $(CFLAGS) -c parmake.c -o parmake.o

queue.o: queue.c queue.h
	$(CC) $(CFLAGS) -c queue.c -o queue.o

parser.o: parser.c parser.h
	$(CC) $(CFLAGS) -c parser.c -o parser.o

rule.o: rule.c rule.h
	$(CC) $(CFLAGS) -c rule.c -o rule.o

clean:
	rm -rf *.o parmake doc/html
