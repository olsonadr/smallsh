
default: smallsh

smallsh: smallsh.c
	gcc -o smallsh smallsh.c

clean:
	rm -r -f smallsh
