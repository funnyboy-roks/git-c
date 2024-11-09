main: $(wildcard src/*)
	gcc -ggdb -Wextra -o main $^ -lz
