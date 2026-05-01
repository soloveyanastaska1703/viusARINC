CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
LDFLAGS = -lm

all: arinc_struct_test arinc_scenarios

arinc_struct_test: arinc429.c arinc_struct_test.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

arinc_scenarios: arinc429.c arinc_scenarios.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	rm -f arinc_struct_test arinc_scenarios

.PHONY: all clean