all: lisp test

test: tests
	./$^

lisp: lisp.c
	$(CC) -I. -o $@ lisp.c

tests: tests.c lisp.c atf.h
	$(CC) -I. -o $@ tests.c

clean:
	$(RM) lisp tests

