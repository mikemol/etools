objects = entwatch entbuff

all: entwatch entbuff

entwatch: entwatch.c
	gcc -g -O0 -o $@ entwatch.c

entbuff: entbuff.c
	gcc -g -O0 -o $@ entbuff.c

.PHONY: all clean

clean:
	-rm $(objects)
