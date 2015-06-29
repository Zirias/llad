CC = gcc
CFLAGS = -O3 -Wall -Werror -pedantic -std=c99

llad_OBJS := obj/llad.o obj/util.o obj/daemon.o obj/config.o obj/logfile.o \
    obj/watcher.o obj/action.o
llad_LIBS := -lpopt

llad: $(llad_OBJS)
	$(CC) -o $@ $^ $(llad_LIBS)

all: llad

clean:
	rm -f llad
	rm -fr obj

obj:
	mkdir obj

obj/%.o: src/%.c | obj
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: all clean

