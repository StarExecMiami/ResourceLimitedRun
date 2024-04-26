CC = gcc
CFLAGS = -Wall -Wextra --pedantic
LFLAGS = -lm

all: ResourceLimitedRun Fibonacci MPFibonacci

ResourceLimitedRun: ResourceLimitedRun.c
	$(CC) $(CFLAGS) -o $@ $@.c $(LFLAGS)

Fibonacci: Fibonacci.c
	$(CC) $(CFLAGS) -o $@ $@.c $(LFLAGS)

MPFibonacci: MPFibonacci.c
	$(CC) $(CFLAGS) -o $@ $@.c $(LFLAGS)
