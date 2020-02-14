FLAGS=-g #-Wall -Wextra -ansi -pedantic

default: smallsh
.PHONY: clean cleanall cleantest test

smallsh: ./src/smallsh.c
	gcc -o ./smallsh ./src/smallsh.c $(FLAGS)

test: smallsh p3testscript
	./p3testscript >results 2>&1
	mv p3testscript utils/

p3testscript:
	mv utils/p3testscript ./

cleanall: clean cleantest

clean:
	rm -r -f ./smallsh

cleantest:
	rm -f results junk junk2
