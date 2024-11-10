#include <openssl/sha.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>
#include <dirent.h>
#include <time.h>

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

    size_t len = strtoul(split, &split, 10);
    split += 1;

    fwrite(split, 1, decomp_len - (split - decomp), stdout);

    free(decomp);
}

typedef enum {
    KD_Blob,
    KD_Tree,
    KD_Commit,
} Kind;

Kind get_kind(const char hex[40]) {
    char buf[256];
    snprintf(buf, 256, ".git/objects/%.2s/%.38s", hex, hex + 2);
    String compr = read_entire_file(buf);
    String content = decomp(compr);
    if (!strncmp(content.items, "blob", 4)) {
        return KD_Blob;
    } else if (!strncmp(content.items, "tree", 4)) {
        return KD_Tree;
    } else if (!strncmp(content.items, "commit", 6)) {
        return KD_Commit;
    } else {
        PANIC("Unknown file %s: %.*s", buf, (int)content.count, content.items);
    }
}

void hash_file(const char *path, u8 hashed[20]) {
    String content = read_entire_file(path);

    hash(content, hashed);
    char hex[41] = { 0 };
    encode_hex(hashed, hex);

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

    char obj_path[256] = { 0 };
    size_t len = snprintf(obj_path, 256, ".git/objects/%.2s", hex);
    mkdir(obj_path, 0755);
    snprintf(obj_path + len, 256, "/%s", hex + 2);

    DBG("obj_path: %s", obj_path);
    FILE *out = fopen(obj_path, "wb");
    if (!out) PANIC("Unable to open file for writing %s: %m", obj_path);

    comp(out, content);

    fclose(out);
defer:
    free(content.items);
}

void git_hash_object(int argc, char **argv) {
    if (argc < 4) PANIC("Usage: %s hash-object -p <file>", argv[0]);
    bool write_obj = !strcmp(argv[2], "-w");
    if (!write_obj) PANIC("-w must be specified.");

    char *path = argv[3];

    u8 hashed[20];
    hash_file(path, hashed);
    char hex[41] = { 0 };
    encode_hex(hashed, hex);

    printf("%s", hex);
}

void git_ls_tree(int argc, char **argv) {
    if (argc < 3) PANIC("Usage: %s ls-tree [--name-only] <tree-hash>", argv[0]);
    bool name_only = !strcmp(argv[2], "--name-only");

    char *hash = name_only ? argv[3] : argv[2];
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

    char *start;
    for (size_t i = 0; i < len; i += split - start) {
        start = split;

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

        if (!name_only) {
            printf("%s ", mode);
            switch (get_kind(hex)) {
                case KD_Blob: printf("blob "); break;
                case KD_Tree: printf("tree "); break;
                case KD_Commit: printf("commit "); break;
            }
            printf("%s    ", hex);
        }
        printf("%s\n", name);

        split += 20;
    }

    free(decomp);
}

void make_tree_for_dir(const char *path, u8 hashed[20]) {
    String s = { 0 };

    struct dirent **namelist;

    int n = scandir(path, &namelist, NULL, alphasort);
    if (n == -1) PANIC("Unable to scan dir %s: %m", path);

    char buf[1024] = { 0 };
    for (int i = 0; i < n; free(namelist[i++])) {
        struct dirent *ent = namelist[i];

        if (
            !strcmp(ent->d_name, ".")
            || !strcmp(ent->d_name, "..")
            || !strcmp(ent->d_name, ".git")
            ) continue;

        snprintf(buf, 1024, "%s/%s", path, ent->d_name);

        struct stat f = { 0 };
        if (stat(buf, &f)) PANIC("Unable to stat file/directory %s: %m", buf);

        u8 hashed_ent[20] = { 0 };

        // snprintf(buf, 1024, "%s/%s/%s", path, ent->d_name);
        char *kind; 
        switch (ent->d_type) {
            case DT_DIR: {
                // DBG("make_tree_for_dir");
                make_tree_for_dir(buf, hashed_ent);
                kind = "tree";
            } break;
            case DT_REG: {
                // DBG("hash_file");
                hash_file(buf, hashed_ent);
                kind = "blob";
            } break;
            default:
                PANIC("Unhandled file type: %d", ent->d_type);
        }
        char hex[41];
        encode_hex(hashed_ent, hex);

        DBG("name = %s", ent->d_name);
        DBG("mode = %06o", f.st_mode)
        snprintf(buf, 1024, "%06o %s", f.st_mode, ent->d_name);
        string_extend_cstr(&s, buf);
        da_append(&s, '\0');
        String hash_str = {
            .items = (char *)hashed_ent,
            .count = 20,
            .capacity = 20,
        };
        string_extend(&s, hash_str);
    }
    free(namelist);
    DBG("s = %.*s", s.count, s.items);

    char prefix[32] = { 0 };
    size_t prefix_len = snprintf(prefix, 32, "tree %ld", s.count) + 1;

    if (s.capacity < s.count + prefix_len) {
        char *new_items = malloc(s.count + prefix_len);
        memcpy(new_items + prefix_len, s.items, s.count);
        memcpy(new_items, prefix, prefix_len);
        s.items = new_items;
        s.count += prefix_len;
    } else {
        memmove(s.items + prefix_len, s.items, s.count);
        memcpy(s.items, prefix, prefix_len);
        s.count += prefix_len;
    }
    
    hash(s, hashed);
    char hex[41];
    encode_hex(hashed, hex);

    char obj_path[256] = { 0 };
    size_t len = snprintf(obj_path, 256, ".git/objects/%.2s", hex);
    int ret = mkdir(obj_path, 0755);
    if (ret) DBG("Error mkdir %s: %m", obj_path);
    snprintf(obj_path + len, 256, "/%s", hex + 2);

    DBG("obj_path: %s", obj_path);
    FILE *out = fopen(obj_path, "wb");
    if (!out) PANIC("Unable to open file for writing %s: %m", obj_path);

    comp(out, s);

    fclose(out);

}

void git_write_tree(int argc, char **argv) {
    if (argc != 2) PANIC("Usage: %s write-tree", argv[0]);

    u8 hashed[20];
    make_tree_for_dir(".", hashed);
    char hex[41];
    encode_hex(hashed, hex);
    printf("%s", hex);
}

void git_commit_tree(int argc, char **argv) {
    if (argc < 3) PANIC("Usage: %s commit-tree <tree-hash> [-p <parent-commit>] -m <message>", argv[0]);

    int opt;
    char *parent = NULL;
    char *message = NULL;
    while ((opt = getopt(argc - 2, argv + 2, "p:m:")) != -1) {
        switch (opt) {
            case 'p':
                parent = strdup(optarg);
                break;
            case 'm':
                message = strdup(optarg);
                break;
            default: exit(1);
        }
    }

    DBG("message = %s", message);
    DBG("parent = %s", parent);
    if (!message) PANIC("Message is required: -m <message>");

    if (parent) {
        if (strlen(parent) != 40) PANIC("parent hash should be 40 chars long.");
        Kind kind = get_kind(parent);

        if (kind != KD_Commit) PANIC("parent should be commit");
    }

    char *tree_hash = argv[2];
    if (strlen(tree_hash) != 40) PANIC("tree-hash should be 40 chars long.");

    Kind kind = get_kind(tree_hash);

    if (kind != KD_Tree) PANIC("Expected hash to reference tree");

    String s = { 0 };
    char buf[256] = { 0 };

    snprintf(buf, 256, "tree %s\n", tree_hash); 
    string_extend_cstr(&s, buf);

    if (parent) {
        snprintf(buf, 256, "parent %s\n", parent); 
        string_extend_cstr(&s, buf);
    }

    snprintf(buf, 256, "author funnyboy-roks <funnyboyroks@gmail.com> %lu -0600\n", (unsigned long)time(NULL)); 
    string_extend_cstr(&s, buf);

    snprintf(buf, 256, "committer funnyboy-roks <funnyboyroks@gmail.com> %lu -0600\n", (unsigned long)time(NULL)); 
    string_extend_cstr(&s, buf);

    da_append(&s, '\n');
    string_extend_cstr(&s, message);
    da_append(&s, '\n');

    DBG("commit = %.*s", s.count, s.items);

    char prefix[32] = { 0 };
    size_t prefix_len = snprintf(prefix, 32, "commit %ld", s.count) + 1;

    if (s.capacity < s.count + prefix_len) {
        char *new_items = malloc(s.count + prefix_len);
        memcpy(new_items + prefix_len, s.items, s.count);
        memcpy(new_items, prefix, prefix_len);
        s.items = new_items;
        s.count += prefix_len;
    } else {
        memmove(s.items + prefix_len, s.items, s.count);
        memcpy(s.items, prefix, prefix_len);
        s.count += prefix_len;
    }
    
    u8 hashed[20];
    hash(s, hashed);
    char hex[41];
    encode_hex(hashed, hex);

    char obj_path[256] = { 0 };
    size_t len = snprintf(obj_path, 256, ".git/objects/%.2s", hex);
    int ret = mkdir(obj_path, 0755);
    if (ret) DBG("Error mkdir %s: %m", obj_path);
    snprintf(obj_path + len, 256, "/%s", hex + 2);

    DBG("obj_path: %s", obj_path);
    FILE *out = fopen(obj_path, "wb");
    if (!out) PANIC("Unable to open file for writing %s: %m", obj_path);

    comp(out, s);
    fclose(out);
    printf("%s", hex);
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
    else if (!strcmp(command, "write-tree"))
        git_write_tree(argc, argv);
    else if (!strcmp(command, "commit-tree"))
        git_commit_tree(argc, argv);
    else PANIC("Unknown subcommand %s", command)
    
    return 0;
}
