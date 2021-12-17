#ifndef ZOS_SEMESTRALKA_UTIL_H
#define ZOS_SEMESTRALKA_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "fs.h"

#define MAX(a, b) (a) > (b) ? (a) : (b)
#define MIN(a, b) (a) < (b) ? (a) : (b)
#define VALIDATE(ret) if (ret) return ret;
#define VALIDATE_MALLOC(data) if (!(data)){ \
fprintf(stderr, "Ran out of memory!\n");                                          \
exit(-1);                                          \
}

/**
 * Removes the new line from the string s
 * @param s the string
 */
void remove_nl(char* s);

/**
 * Lists out all FS and SB params
 */
void debug();

/**
 * Parses the number or exits if the number could not be parsed
 * @param s the number
 * @return
 */
uint32_t parse(const char* s);

/**
 * Parses the dir
 * @param dir the full dir name
 * @param out_dir the dir name ptr
 * @param prev_dir the ptr to prev_dir
 * @param cur_dir the ptr to cur_dir
 * @return the dir inode ID
 */
void parse_dir(const char* dir, struct entry* prev_dir, struct entry* cur_dir);

void parse_cmd(FILE* stream);

#endif //ZOS_SEMESTRALKA_UTIL_H
