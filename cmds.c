#include "cmds.h"
#include "consts.h"
#include <stdio.h>

int format(char* s[]) {
    return OK;
}

int cp(char* s[]) {
    char* s1;
    char* s2;
    s1 = s[0];
    s2 = s[1];

    return OK;
}

int mv(char* s[]) {
    return OK;
}

int rm(char* s[]) {
    return OK;
}

int mkdir(char* a[]) {
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
    return OK;
}

int xcp(char* s[]) {
    return OK;
}

int short_cmd(char* s[]) {
    return OK;
}
