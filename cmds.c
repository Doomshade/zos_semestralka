#include "cmds.h"
#include "consts.h"
#include <stdio.h>
#include <string.h>
#include "util.h"

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
    uint32_t curr_dir;
    const char* aa = a[0];

    if (strncmp(aa, "/", 1) == 0) {
        curr_dir = FS->root;
    } else {
        curr_dir = FS->curr_dir;
    }
    return OK;
}

int rmdir(char* a[]) {
    return OK;
}

int ls(char* a[]) {
    return CUSTOM_OUTPUT;
}

int cat(char* s[]) {
    return CUSTOM_OUTPUT;
}

int cd(char* a[]) {
    return OK;
}

int pwd(char* empt[]) {
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
