#ifndef ZOS_SEMESTRALKA_CMDS_H
#define ZOS_SEMESTRALKA_CMDS_H
#include <stdint.h>

typedef struct {

} FS;

int cp(char* s1, char* s2);

int mv(char* s1, char* s2);

int rm(char* s1);

int mkdir(char* a1);

int rmdir(char* a1);

int ls(char* a1);

int cat(char* s1);

int cd(char* a1);

int pwd();

int info(char* as1);

int incp(char* s1, char* s2);

int outcp(char* s1, char* s2);

int load(char* s1);

int format(int32_t size);

int xcp(char* s1, char* s2, char* s3);

int short_cmd(char* s1);

#endif
