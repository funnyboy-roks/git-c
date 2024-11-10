#include <openssl/sha.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <zlib.h>

#include "util.h"

void string_extend(String *dst, String src) {
    if (dst->count + src.count > dst->capacity) {
        dst->items = realloc(dst->items, dst->count + src.count);
        dst->capacity = dst->count + src.count;
    }
    memcpy(dst->items + dst->count, src.items, src.count);
    dst->count += src.count;
}

void string_extend_cstr(String *dst, char *src) {
    size_t len = strlen(src);
    String s = {
        .items = src,
        .count = len,
        .capacity = len,
    };
    string_extend(dst, s);
}

String read_entire_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL)                 PANIC("Cannot open for reading %s: %m", path);
    if (fseek(f, 0, SEEK_END) < 0) PANIC("seek failed: %m");
    long m = ftell(f);
    if (m < 0)                     PANIC("m < 0: %m");
    if (fseek(f, 0, SEEK_SET) < 0) PANIC("seek failed: %m");

    String s = {
        .items = malloc(m),
        .capacity = m,
        .count = 0,
    };

    fread(s.items, m, 1, f);
    if (ferror(f)) PANIC("Cannot read file %s: %m", path);
    s.count = m;

    return s;
}

void hash(String s, u8 hash[20]) {
    SHA1((u8 *)s.items, s.count, (u8 *)hash);
}

void encode_hex(u8 hash[20], char hex[41]) {
    for (size_t i = 0; i < 20; ++i)
        sprintf(hex + i * 2, "%02x", hash[i]);
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

String decomp(String content) {
    z_stream d_stream;

    d_stream.next_in  = (u8 *)content.items;
    d_stream.avail_in = content.count;

    d_stream.zalloc = zalloc;
    d_stream.zfree = zfree;


    if (inflateInit(&d_stream) != Z_OK) PANIC("inflateInit");

    String s = { 0 };

    Byte buf[BUF_SIZE] = { 0 };
    while (d_stream.total_in < content.count) {
        d_stream.next_out = buf;
        d_stream.avail_out = BUF_SIZE;

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

void comp(FILE *out, String content) {
    z_stream c_stream;

    c_stream.next_in  = (u8 *)content.items;
    c_stream.avail_in = content.count;

    c_stream.zalloc = Z_NULL;
    c_stream.zfree = Z_NULL;


    if (deflateInit(&c_stream, Z_DEFAULT_COMPRESSION) != Z_OK) PANIC("deflateInit");

    u8 buf[BUF_SIZE] = { 0 };
    do {
        c_stream.avail_out = BUF_SIZE;
        c_stream.next_out = buf;
        int err = deflate(&c_stream, Z_NO_FLUSH);
        if (err == Z_STREAM_ERROR) PANIC("deflate error (%d)", err)
        size_t have = BUF_SIZE - c_stream.avail_out;
        if (fwrite(buf, 1, have, out) != have || ferror(out)) PANIC("Error writing compressed data: %m");
    } while (c_stream.avail_out == 0);

    int err;
    do {
        c_stream.avail_out = BUF_SIZE;
        c_stream.next_out = buf;
        err = deflate(&c_stream, Z_FINISH);
        if (err != Z_STREAM_END && err != Z_OK) PANIC("deflate, err = %d", err);

        fwrite(buf, 1, BUF_SIZE - c_stream.avail_out, out);
    } while (err != Z_STREAM_END);

    err = deflateEnd(&c_stream);
    if (err != Z_OK) PANIC("deflateEnd, err = %d", err);
}
