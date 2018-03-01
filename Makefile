CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lpthread
HEADERS = private.h object.h list.h
OBJECTS = main.o object.o list.o

all: main

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

main: $(OBJECTS)
	gcc $(CFLAGS) $(OBJECTS) $(LDFLAGS) -o $@

clean:
	rm -f $(OBJECTS)
	rm -f main bob fred
