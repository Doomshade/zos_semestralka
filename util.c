#include "util.h"
#include "string.h"

void remove_nl(char* s) {
    s[strcspn(s, "\n")] = 0;
}