#include "cmds.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "util.h"
#include "cmd_handler.h"
#include <stdlib.h>

int format(char* s[]) {
    uint32_t size;
    size = parse(s[0]);
    printf("%u\n", size);
    return fs_format(size) == 0 ? OK : ERR_CANNOT_CREATE_FILE;
}

int cp(char* s[]) {
    return OK;
}

int mv(char* s[]) {
    return OK;
}

int rm(char* s[]) {
    return OK;
}

int mkdir(char* a[]) {
    struct entry dir;
    struct entry parent;

    parse_dir(a[0], &parent, &dir);
    if (parent.inode_id == FREE_INODE) {
        return ERR_PATH_NOT_FOUND;
    }
    if (dir.inode_id != FREE_INODE) {
        return ERR_EXIST;
    }
    return create_dir(parent.inode_id, dir.item_name) != FREE_INODE ? OK : ERR_UNKNOWN;
}

int rmdir(char* a[]) {
    return OK;
}

int ls(char* a[]) {
    struct entry dir;
    struct entry parent;
    struct entry* entries;
    struct inode* inode;
    uint32_t amount;
    uint32_t i;

    parse_dir(a[0], &parent, &dir);
    if (dir.inode_id == FREE_INODE) {
        return ERR_PATH_NOT_FOUND;
    }
    amount = get_dir_entries(dir.inode_id, &entries);
    for (i = 0; i < amount; ++i) {
        inode = inode_get(entries[i].inode_id);
        printf("%s%s\n", inode->file_type == FILE_TYPE_REGULAR_FILE ? "-" : "+", entries[i].item_name);
    }

    return CUSTOM_OUTPUT;
}

int cat(char* s[]) {
    return CUSTOM_OUTPUT;
}

int cd(char* a[]) {
    struct entry dir;
    struct entry parent;

    parse_dir(a[0], &parent, &dir);

    if (dir.inode_id == FREE_INODE) {
        return ERR_PATH_NOT_FOUND;
    }
    FS->curr_dir = dir.inode_id;
    return OK;
}

typedef struct NODE {
    struct NODE* next;
    struct entry entry;
} node;

int pwd(char* empt[]) {
    node* n;
    node* prev;
    struct entry parent;
    struct entry dir;
    struct entry entry;
    uint32_t curr_dir;

    // coding this was pain, please don't ask me how I did this
    // it was nothing but bunch of debugging
    curr_dir = FS->curr_dir;
    prev = malloc(sizeof(node));
    VALIDATE_MALLOC(prev)

    *prev = (node) {.next = NULL, .entry = (struct entry) {.inode_id = FS->curr_dir, .item_name = CURR_DIR}};
    dir.inode_id = 0;
    while (parent.inode_id != FS->root) {
        parse_dir(PREV_DIR, &dir, &parent);
        n = malloc(sizeof(node));
        VALIDATE_MALLOC(n)

        get_dir_entry(parent.inode_id, &entry, dir.inode_id);

        n->entry = entry;
        n->next = prev;
        prev = n;
        FS->curr_dir = parent.inode_id;
    }

    while (strcmp(prev->entry.item_name, CURR_DIR) != 0) {
        printf("/%s", prev->entry.item_name);
        prev = prev->next;
    }
    printf("\n");
    FS->curr_dir = curr_dir;
    return CUSTOM_OUTPUT;
}

int info(char* as[]) {
    return CUSTOM_OUTPUT;
}

int incp(char* s[]) {
    return OK;
}

int outcp(char* s[]) {
    return OK;
}

int load(char* s[]) {
    FILE* f;

    f = fopen(s[0], "r");
    if (!f) {
        return ERR_FILE_NOT_FOUND;
    }

    while (!feof(f)) {
        parse_cmd(f);
    }
    return OK;
}

int xcp(char* s[]) {
    return OK;
}

int short_cmd(char* s[]) {
    return OK;
}
