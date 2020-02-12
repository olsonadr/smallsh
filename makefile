FLAGS=-g #-Wall -Wextra -ansi -pedantic

default: smallsh

smallsh: smallsh.c
	gcc -o smallsh smallsh.c $(FLAGS)

clean:
	rm -r -f smallsh
