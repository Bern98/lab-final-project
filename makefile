
CC=gcc
CFLAGS=-g -Wall -pedantic
LDLIBS= -lm -pthread
DEPS=unboundedqueue.h errmsg.h sfiles.h
OBJS=unboundedqueue.o errmsg.o sfiles.o

# eseguibile da compilare
MAIN=main

all: $(MAIN)

$(MAIN): $(MAIN).o $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(MAIN).o: $(MAIN).c $(DEPS)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# target che cancella eseguibili e file oggetto
clean:
	rm -f $(MAIN) *.o

# target che esegue test 1
test1: $(MAIN)
	./$(MAIN) 1 dirs
	@echo ran with 1 threads

# target che esegue test 2
test2: $(MAIN)
	./$(MAIN) 3 dirs
	@echo ran with 3 threads
