
sgregex_test_cc: sgregex_test.c sgregex.c sgregex.h
	gcc -o $@ sgregex_test.c -g -std=c89 -Wall -Wpedantic -Wconversion -Wshadow -Wpointer-arith -Wcast-qual -Wcast-align

dotest: sgregex_test_cc
	./sgregex_test_cc

vgtest: sgregex_test_cc
	valgrind --leak-check=full ./sgregex_test_cc

drmtest: sgregex_test_cc
	drmemory -- ./sgregex_test_cc

