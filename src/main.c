#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>
#include <assert.h>
#include <errno.h>

#define _PRINT_MSG(prefix, ...) do {              \
    fprintf(stderr, prefix" %s:%d ", __FILE__, __LINE__);  \
    fprintf(stderr, __VA_ARGS__);                          \
    fprintf(stderr, "\n");                                 \
} while(0);                                       \

#define DBG(...) _PRINT_MSG("[DBG]", __VA_ARGS__)
#define PANIC(...) do {                           \
    _PRINT_MSG("[PANIC]", __VA_ARGS__);           \
    exit(1);                                      \
} while (0);

#define nob_da_append(da, item)                                                      \
    do {                                                                             \
        if ((da)->count >= (da)->capacity) {                                         \
            (da)->capacity = (da)->capacity == 0 ? 1024 : (da)->capacity*2;          \
            (da)->items = realloc((da)->items, (da)->capacity*sizeof(*(da)->items)); \
            assert((da)->items != NULL && "Buy more RAM lol");                       \
        }                                                                            \
                                                                                     \
        (da)->items[(da)->count++] = (item);                                         \
    } while (0)

void git_init(void) {
    if (mkdir(".git", 0755))         PANIC("mkdir .git: %m");
    if (mkdir(".git/objects", 0755)) PANIC("mkdir .git/objects: %m");
    if (mkdir(".git/refs", 0755))    PANIC("mkdir .git/refs: %m");

    FILE *headFile = fopen(".git/HEAD", "wb");
    if (!headFile) PANIC("Cannot open for writing '.git/HEAD': %m")
        fprintf(headFile, "ref: refs/heads/main\n");
    fclose(headFile);

    printf("Initialized git directory\n");
}

typedef struct {
    char *items;
    size_t count;
    size_t capacity;
} String;

String read_entire_file(const char *path) {
    String s = { 0 };
    FILE *f = fopen(path, "rb");
    if (f == NULL)                 PANIC("Cannot open for reading %s: %m", path);
    if (fseek(f, 0, SEEK_END) < 0) PANIC("seek failed: %m");
    long m = ftell(f);
    if (m < 0)                     PANIC("m < 0: %m");
    if (fseek(f, 0, SEEK_SET) < 0) PANIC("seek failed: %m");

    size_t new_count = s.count + m;
    if (new_count > s.capacity) {
        s.items = realloc(s.items, new_count);
        assert(s.items != NULL && "Buy more RAM lool!!");
        s.capacity = new_count;
    }

    fread(s.items + s.count, m, 1, f);
    if (ferror(f)) PANIC("Cannot read file %s: %m", path);
    s.count = new_count;

    return s;
}

#define BUF_SIZE 1024

static void *zalloc(void *q, unsigned n, unsigned m) {
    (void)q;
    return calloc(n, m);
}

static void zfree(void *q, void *p) {
    (void)q;
    free(p);
}

String de(Byte *compr, size_t compr_len) {
    z_stream d_stream;

    d_stream.next_in  = compr;
    d_stream.avail_in = compr_len;

    d_stream.zalloc = zalloc;
    d_stream.zfree = zfree;


    if (inflateInit(&d_stream) != Z_OK) PANIC("inflateInit");

    String s = { 0 };

    DBG("d_stream.total_in = %ld", d_stream.total_in);
    DBG("d_stream.avail_in = %d", d_stream.avail_in);
    DBG("compr_len = %ld", compr_len);

    Byte buf[BUF_SIZE] = { 0 };
    while (d_stream.total_in < compr_len) {
        d_stream.next_out = buf;
        d_stream.avail_out = BUF_SIZE;

        DBG("a");

        int err = inflate(&d_stream, Z_NO_FLUSH);
        if (err != Z_STREAM_END && err != Z_OK) PANIC("inflate");

        if (d_stream.total_out > s.capacity) {
            s.items = realloc(s.items, d_stream.total_out);
            assert(s.items != NULL && "Buy more RAM lool!!");
            s.capacity = d_stream.total_out;
        }

        memcpy(s.items + s.count, buf, d_stream.total_out - s.count);
        s.count = d_stream.total_out;
        if (err == Z_STREAM_END) break;
    }

    if (inflateEnd(&d_stream) != Z_OK) PANIC("inflateEnd");

    return s;
}

void git_cat_file(int argc, char **argv) {
    if (argc < 4) PANIC("Usage: %s cat-file -p <file>", argv[0]);
    if (strcmp(argv[2], "-p")) PANIC("Must pass -p to cat-file");

    char *hash = argv[3];
    char hash_len = strlen(hash);
    if (hash_len < 2) PANIC("Invalid hash '%s'", hash);

    char buf[256] = { 0 };
    snprintf(buf, 256, ".git/objects/%.2s/%s", hash, hash + 2);
    DBG("path: %s", buf);

    String content = read_entire_file(buf);

    String decomp_str = de((Byte *)content.items, content.count);
    free(content.items);

    char *decomp = decomp_str.items;
    size_t decomp_len = decomp_str.count;

    DBG("%.*s", decomp_str.count, decomp_str.items);

    char *kind = decomp;
    char *split = strchr(decomp, ' ');
    if (!split) PANIC("Expected space in decompressed file");
    *split = '\0';
    split += 1;
    DBG("kind = %s", kind);

    size_t len = strtoul(split, &split, 10);
    split += 1;

    fwrite(split, 1, decomp_len - (split - decomp), stdout);

    free(decomp);
}

int main(int argc, char **argv) {
    if (argc < 2) PANIC("Usage: %s <subcommand>", argv[0]);
    
    char *command = argv[1];

    if (!strcmp(command, "init"))
        git_init();
    else if (!strcmp(command, "cat-file"))
        git_cat_file(argc, argv);
    else PANIC("Unknown subcommand %s", command)
    
    return 0;
}
