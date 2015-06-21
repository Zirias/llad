llad_OBJS := obj/llad.o obj/daemon.o
llad_LIBS := -lpopt

llad: $(llad_OBJS)
	gcc -o $@ $^ $(llad_LIBS)

all: llad

clean:
	rm -f llad
	rm -fr obj

obj:
	mkdir obj

obj/%.o: src/%.c | obj
	gcc -c -o $@ $<

.PHONY: all clean

