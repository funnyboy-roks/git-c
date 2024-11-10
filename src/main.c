#include <openssl/sha.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "util.h"

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


void git_cat_file(int argc, char **argv) {
    if (argc < 4) PANIC("Usage: %s cat-file -p <file>", argv[0]);
    if (strcmp(argv[2], "-p")) PANIC("Must pass -p to cat-file");

    char *hash = argv[3];
    char hash_len = strlen(hash);
    if (hash_len < 2) PANIC("Invalid hash '%s'", hash);

    char path[256] = { 0 };
    snprintf(path, 256, ".git/objects/%.2s/%s", hash, hash + 2);
    DBG("path: %s", path);

    String content = read_entire_file(path);

    String decomp_str = decomp(content);
    free(content.items);

    char *decomp = decomp_str.items;
    size_t decomp_len = decomp_str.count;

    DBG("%.*s", (int)decomp_str.count, decomp_str.items);

    char *kind = decomp;
    char *split = strchr(decomp, ' ');
    if (!split) PANIC("Expected space in decompressed file");
    *split = '\0';
    split += 1;
    DBG("kind = %s", kind);

    if (strcmp(kind, "blob")) PANIC("Unsupported kind '%s'", kind);

    size_t len = strtoul(split, &split, 10);
    split += 1;

    fwrite(split, 1, decomp_len - (split - decomp), stdout);

    free(decomp);
}

void git_hash_object(int argc, char **argv) {
    if (argc < 4) PANIC("Usage: %s hash-object -p <file>", argv[0]);
    bool write_obj = !strcmp(argv[2], "-w");

    char *path = write_obj ? argv[3] : argv[2];

    String content = read_entire_file(path);

    u8 hashed[20];
    hash(content, hashed);
    char hex[41] = { 0 };
    encode_hex(hashed, hex);

    DBG("hex: %s", hex);
    
    DBG("write_obj: %s", write_obj ? "true" : "false");
    if (!write_obj) goto defer;

    char obj_path[256] = { 0 };
    size_t len = snprintf(obj_path, 256, ".git/objects/%.2s", hex);
    if (mkdir(obj_path, 0755)) PANIC("Unable to create directory %s: %m", obj_path);
    snprintf(obj_path + len, 256, "/%s", hex + 2);
    // snprintf(obj_path, 256, "test-obj");
    DBG("obj_path: %s", obj_path);

    char prefix[32] = { 0 };
    size_t prefix_len = snprintf(prefix, 32, "blob %ld", content.count) + 1;

    if (content.capacity < content.count + prefix_len) {
        char *new_items = malloc(content.count + prefix_len);
        memcpy(new_items + prefix_len, content.items, content.count);
        memcpy(new_items, prefix, prefix_len);
        content.items = new_items;
        content.count += prefix_len;
    } else {
        memmove(content.items + prefix_len, content.items, content.count);
        memcpy(content.items, prefix, prefix_len);
        content.count += prefix_len;
    }

    FILE *out = fopen(obj_path, "wb");
    if (!out) PANIC("Unable to open file for writing %s: %m", obj_path);

    comp(out, content);

    fclose(out);
defer:
    free(content.items);
}

void git_ls_tree(int argc, char **argv) {
    if (argc < 4) PANIC("Usage: %s ls-tree --name-only <tree-hash>", argv[0]);
    if (strcmp(argv[2], "--name-only")) PANIC("Must pass --name-only to ls-tree");

    char *hash = argv[3];
    char hash_len = strlen(hash);
    if (hash_len < 2) PANIC("Invalid hash '%s'", hash);

    char path[256] = { 0 };
    snprintf(path, 256, ".git/objects/%.2s/%s", hash, hash + 2);

    String content = read_entire_file(path);

    String decomp_str = decomp(content);
    free(content.items);

    char *decomp = decomp_str.items;
    size_t decomp_len = decomp_str.count;

    char *kind = decomp;
    char *split = strchr(decomp, ' ');
    if (!split) PANIC("Expected space in decompressed file");
    *split = '\0';
    split += 1;

    if (strcmp(kind, "tree")) PANIC("Expected tree, got '%s'", kind);

    size_t len = strtoul(split, &split, 10);
    split += 1;

    char *tree = split;

    for (size_t i = 0; i < len; ) {
        char *start = split;

        char *mode = start;
        split = strchr(split, ' ');
        if (!split) PANIC("Expected space in decompressed file");
        *split = '\0';
        split += 1;
        char *name = split;

        split += strlen(split) + 1;
        u8 *hash = (u8 *)split;
        char hex[41];
        encode_hex(hash, hex);

        printf("%s\n", name);

        split += 20;

        i += split - start;
    }

    free(decomp);
}

int main(int argc, char **argv) {
    if (argc < 2) PANIC("Usage: %s <subcommand>", argv[0]);

    char *command = argv[1];

    if (!strcmp(command, "init"))
        git_init();
    else if (!strcmp(command, "cat-file"))
        git_cat_file(argc, argv);
    else if (!strcmp(command, "hash-object"))
        git_hash_object(argc, argv);
    else if (!strcmp(command, "ls-tree"))
        git_ls_tree(argc, argv);
    else PANIC("Unknown subcommand %s", command)
    
    return 0;
}
