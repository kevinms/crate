CC = gcc
CFLAGS = -Wall -g -Werror
LDFLAGS = -lpthread

SOURCES=main.c crate.c list.c
OBJECTS=$(SOURCES:.c=.o)

all: main

main: $(OBJECTS)
	gcc $(CFLAGS) $(OBJECTS) $(LDFLAGS) -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS)
	rm -f main bob fred
