#include <stdio.h>
#include <zlib.h>
#include <assert.h>

typedef Byte u8;

#define _PRINT_MSG(prefix, ...) do {              \
    fprintf(stderr, prefix" %s:%d ", __FILE__, __LINE__);  \
    fprintf(stderr, __VA_ARGS__);                          \
    fprintf(stderr, "\n");                                 \
} while(0);                                       \

#ifndef RELEASE
#define DBG(...) _PRINT_MSG("[DBG]", __VA_ARGS__)
#else // RELEASE
#define DBG(...) 0;
#endif
#define PANIC(...) do {                           \
    _PRINT_MSG("[PANIC]", __VA_ARGS__);           \
    exit(1);                                      \
} while (0);

#define da_append(da, item)                                                          \
    do {                                                                             \
        if ((da)->count >= (da)->capacity) {                                         \
            (da)->capacity = (da)->capacity == 0 ? 1024 : (da)->capacity*2;          \
            (da)->items = realloc((da)->items, (da)->capacity*sizeof(*(da)->items)); \
            assert((da)->items != NULL && "Buy more RAM lol");                       \
        }                                                                            \
                                                                                     \
        (da)->items[(da)->count++] = (item);                                         \
    } while (0)

typedef struct {
    char *items;
    size_t count;
    size_t capacity;
} String;

void string_extend(String *dst, String src);
void string_extend_cstr(String *dst, char *src);

void hash(String s, u8 hash[20]);
void encode_hex(u8 hash[20], char hex[41]);

String read_entire_file(const char *path);
String decomp(String compressed);
void comp(FILE *out, String uncompressed);
