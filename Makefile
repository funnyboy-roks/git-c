CFLAGS = -ggdb -Wextra $(shell pkg-config --cflags openssl zlib)
LIBFLAGS = $(shell pkg-config --libs openssl zlib)

main: $(wildcard src/*)
	gcc $(CFLAGS) -o main $^ $(LIBFLAGS)

release: $(wildcard src/*)
	gcc $(CFLAGS) -O3 -DRELEASE -o release $^ $(LIBFLAGS)
