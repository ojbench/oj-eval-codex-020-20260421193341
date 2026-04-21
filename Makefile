.PHONY: all clean
all:
	gcc -O2 -std=gnu11 -o code main.c buddy.c

clean:
	rm -f code
