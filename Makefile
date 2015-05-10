all:
		gcc -Wall -Wextra -pedantic -std=c99 memxamine.c -o memxamine

debug:
		gcc -g -Wall -Wextra -pedantic -std=c99 memxamine.c -o memxamine

.PHONY: clean
clean:
		rm memxamine
