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
    uint32_t dir;
    uint32_t parent;

    parse_dir(&a[0], &parent, &dir);
    if (parent == FREE_INODE) {
        return ERR_PATH_NOT_FOUND;
    }
    if (dir != FREE_INODE) {
        return ERR_EXIST;
    }
    return create_dir(parent, a[0]) != FREE_INODE ? OK : ERR_UNKNOWN;
}

int rmdir(char* a[]) {
    return OK;
}

int ls(char* a[]) {
    uint32_t dir;
    uint32_t parent;
    struct entry* entries;
    struct inode* inode;
    uint32_t amount;
    uint32_t i;

    parse_dir(&a[0], &parent, &dir);
    if (dir == FREE_INODE) {
        return ERR_PATH_NOT_FOUND;
    }
    amount = get_dir_entries(dir, &entries);
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
    uint32_t dir;
    uint32_t parent;

    parse_dir(&a[0], &parent, &dir);

    if (dir == FREE_INODE){
        return ERR_PATH_NOT_FOUND;
    }
    FS->curr_dir = dir;
    return OK;
}

int pwd(char* empt[]) {
    const char CURR_DIR[] = ".";
    char** stack;
    struct inode* inode;
    struct entry* entries;
    uint32_t parent;
    uint32_t dir;

    stack = malloc(sizeof(char*));
    VALIDATE_MALLOC(stack)
    get_dir_entries(FS->curr_dir, &entries);



    free(stack);

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
