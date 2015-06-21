CC = gcc

llad_OBJS := obj/llad.o obj/daemon.o
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
	$(CC) -c -o $@ $<

.PHONY: all clean

