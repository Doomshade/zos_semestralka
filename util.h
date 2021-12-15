#ifndef ZOS_SEMESTRALKA_UTIL_H
#define ZOS_SEMESTRALKA_UTIL_H

#include <stdint.h>
#include <stdio.h>

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
uint32_t parse(char* s);

uint32_t parse_disk_size(char* s);


void parse_cmd(FILE* stream);

#endif //ZOS_SEMESTRALKA_UTIL_H
