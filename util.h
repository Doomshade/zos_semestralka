#ifndef ZOS_SEMESTRALKA_UTIL_H
#define ZOS_SEMESTRALKA_UTIL_H

#define VALIDATE(ret) if (ret) return ret;
#define VALIDATE_MALLOC(data) if (!data) return -1;

/**
 * Removes the new line from the string s
 * @param s the string
 */
void remove_nl(char* s);

#endif //ZOS_SEMESTRALKA_UTIL_H
