
test: regex.c regex.h test.c
	gcc -o test test.c -g -std=c89 -Wall -Wpedantic -Wconversion -Wshadow -Wpointer-arith -Wcast-qual -Wcast-align

dotest: test
	./test

vgtest: test
	valgrind --leak-check=full ./test

