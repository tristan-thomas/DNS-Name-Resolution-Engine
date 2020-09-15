HEADERS = multi-lookup.h util.h
OBJECTS = multi-lookup.o util.o

default: multi-lookup

%.o: %.c $(HEADERS)
	gcc -Wall -Wextra -c $< -o $@

multi-lookup: $(OBJECTS)
	gcc $(OBJECTS) -o $@ -lpthread -lrt

clean:
	-rm -f $(OBJECTS)
	-rm -f multi-lookup results.txt serviced.txt
