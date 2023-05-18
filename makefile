
CC=gcc
CFLAGS=-g -Wall -pedantic 
LDLIBS= -lm

# eseguibile da compilare
MAIN=main

all: $(MAIN)

$(MAIN): $(MAIN).o functions.c

# target che cancella eseguibili e file oggetto
clean:
	rm -f $(MAIN) *.o

# target che esegue test 1
test1: $(MAIN)
	./$(MAIN) 3 dirs
	@echo ran with 3 threads

# target che esegue test 2
test2: $(MAIN)
	./$(MAIN) >> output.txt

