#ifndef ZOS_SEMESTRALKA_CMDS_H
#define ZOS_SEMESTRALKA_CMDS_H
#include <stdint.h>
#include "fs.h"

void setup_fs(struct fs fs);

int cp(char* s[]);

int mv(char* s[]);

int rm(char* s[]);

int mkdir(char* a[]);

int rmdir(char* a[]);

int ls(char* a[]);

int cat(char* s[]);

int cd(char* a[]);

int pwd(char* empt[]);

int info(char* as[]);

int incp(char* s[]);

int outcp(char* s[]);

int load(char* s[]);

int format(char* s[]);

int xcp(char* s[]);

int short_cmd(char* s[]);

#endif
